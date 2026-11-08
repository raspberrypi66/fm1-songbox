#include "smf.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#define MAX_FILE_BYTES (4 * 1024 * 1024)
#define MAX_TRACKS     256
#define DEFAULT_TEMPO  500000   // us per quarter note, 120 BPM

typedef struct {
    const uint8_t *data;
    size_t len;
    uint32_t count;   // events in this track
    uint32_t first;   // its first slot in the unmerged array
} trk_t;

static void *ps_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

// Variable-length quantity, at most 4 bytes
static bool vlq(const uint8_t *p, size_t len, size_t *i, uint32_t *out)
{
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
        if (*i >= len) {
            return false;
        }
        uint8_t b = p[(*i)++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) {
            *out = v;
            return true;
        }
    }
    return false;
}

static esp_err_t check_header(const uint8_t *h, size_t len, uint16_t *format, uint16_t *ntrks, uint16_t *ppqn)
{
    if (len < 14 || memcmp(h, "MThd", 4) != 0 || be32(h + 4) < 6) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint16_t fmt = be16(h + 8);
    uint16_t div = be16(h + 12);
    if (div & 0x8000) {
        return ESP_ERR_NOT_SUPPORTED;   // SMPTE timebase
    }
    if (fmt > 1) {
        return ESP_ERR_NOT_SUPPORTED;   // format 2
    }
    if (div == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *format = fmt;
    *ntrks = be16(h + 10);
    *ppqn = div;
    return ESP_OK;
}

// Parse one MTrk chunk. With out == NULL it only counts. A broken track is
// played up to the point where it breaks.
static uint32_t parse_track(const uint8_t *p, size_t len, mev_t *out, uint32_t *end_tick)
{
    size_t i = 0;
    uint32_t tick = 0, n = 0;
    uint8_t running = 0;

    while (i < len) {
        uint32_t delta;
        if (!vlq(p, len, &i, &delta) || i >= len) {
            break;
        }
        tick += delta;
        uint8_t b = p[i];

        if (b == 0xFF) {   // meta event: only Set Tempo and End of Track matter
            if (i + 2 > len) {
                break;
            }
            uint8_t type = p[i + 1];
            i += 2;
            uint32_t mlen;
            if (!vlq(p, len, &i, &mlen) || mlen > len - i) {
                break;
            }
            if (type == 0x51 && mlen == 3) {
                if (out) {
                    out[n] = (mev_t){ .tick = tick, .status = MEV_TEMPO, .d1 = p[i], .d2 = p[i + 1], .d3 = p[i + 2] };
                }
                n++;
            }
            i += mlen;
            running = 0;
            if (type == 0x2F) {
                break;
            }
            continue;
        }
        if (b == 0xF0 || b == 0xF7) {   // SysEx: skipped, never sent
            i++;
            uint32_t slen;
            if (!vlq(p, len, &i, &slen) || slen > len - i) {
                break;
            }
            i += slen;
            running = 0;
            continue;
        }

        uint8_t status;
        if (b & 0x80) {
            if (b > 0xEF) {
                break;   // stray system message
            }
            status = running = b;
            i++;
        } else if (running) {
            status = running;   // running status
        } else {
            break;   // data byte with no status before it
        }
        int dlen = midi_msg_len(status) - 1;
        if (i + (size_t)dlen > len) {
            break;
        }
        if (out) {
            out[n] = (mev_t){
                .tick = tick,
                .status = status,
                .d1 = p[i] & 0x7F,
                .d2 = dlen == 2 ? (p[i + 1] & 0x7F) : 0,
            };
        }
        n++;
        i += (size_t)dlen;
    }
    *end_tick = tick;
    return n;
}

static esp_err_t read_file(const char *path, uint8_t **out, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 14) {
        fclose(f);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (n > MAX_FILE_BYTES) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *buf = ps_alloc((size_t)n);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = buf;
    *size = (size_t)n;
    return ESP_OK;
}

esp_err_t smf_check(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t h[14];
    size_t got = fread(h, 1, sizeof(h), f);
    fclose(f);
    uint16_t fmt, ntrks, ppqn;
    return check_header(h, got, &fmt, &ntrks, &ppqn);
}

esp_err_t smf_load(const char *path, smf_song_t *song)
{
    memset(song, 0, sizeof(*song));
    uint8_t *buf = NULL;
    size_t size = 0;
    esp_err_t err = read_file(path, &buf, &size);
    if (err != ESP_OK) {
        return err;
    }
    uint16_t fmt, ntrks, ppqn;
    err = check_header(buf, size, &fmt, &ntrks, &ppqn);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    // Find the MTrk chunks (skip unknown chunks) and count their events
    trk_t *trk = calloc(MAX_TRACKS, sizeof(trk_t));
    if (!trk) {
        free(buf);
        return ESP_ERR_NO_MEM;
    }
    int nt = 0;
    uint32_t total = 0, end_tick = 0;
    size_t pos = 8 + be32(buf + 4);
    while (pos + 8 <= size && nt < MAX_TRACKS) {
        size_t clen = be32(buf + pos + 4);
        if (clen > size - pos - 8) {
            clen = size - pos - 8;   // truncated file: use what is there
        }
        if (memcmp(buf + pos, "MTrk", 4) == 0) {
            uint32_t et;
            trk[nt].data = buf + pos + 8;
            trk[nt].len = clen;
            trk[nt].count = parse_track(trk[nt].data, clen, NULL, &et);
            trk[nt].first = total;
            total += trk[nt].count;
            if (et > end_tick) {
                end_tick = et;
            }
            nt++;
        }
        pos += 8 + clen;
    }
    if (nt == 0) {
        free(trk);
        free(buf);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Fill each track's events, then merge them into one time-ordered list.
    // One slot more for the end marker.
    mev_t *all = ps_alloc((total + 1) * sizeof(mev_t));
    mev_t *merged = nt > 1 ? ps_alloc((total + 1) * sizeof(mev_t)) : all;
    uint32_t *cur = calloc((size_t)nt, sizeof(uint32_t));
    if (!all || !merged || !cur) {
        free(cur);
        if (merged != all) {
            free(merged);
        }
        free(all);
        free(trk);
        free(buf);
        return ESP_ERR_NO_MEM;
    }
    for (int t = 0; t < nt; t++) {
        uint32_t et;
        parse_track(trk[t].data, trk[t].len, all + trk[t].first, &et);
    }
    free(buf);

    if (nt > 1) {
        // Each track is already in order; on equal ticks the lower track goes first
        for (uint32_t k = 0; k < total; k++) {
            int best = -1;
            uint32_t best_tick = UINT32_MAX;
            for (int t = 0; t < nt; t++) {
                if (cur[t] < trk[t].count) {
                    uint32_t tk = all[trk[t].first + cur[t]].tick;
                    if (tk < best_tick) {
                        best_tick = tk;
                        best = t;
                    }
                }
            }
            merged[k] = all[trk[best].first + cur[best]++];
        }
        free(all);
    }
    free(cur);
    free(trk);

    // Walk the tempo map for the length, then add the end marker: a tempo
    // event (same tempo) at the End of Track tick
    uint32_t tempo = DEFAULT_TEMPO, last = 0;
    uint64_t us = 0;
    for (uint32_t k = 0; k < total; k++) {
        const mev_t *e = &merged[k];
        us += (uint64_t)(e->tick - last) * tempo / ppqn;
        last = e->tick;
        if (e->status == MEV_TEMPO) {
            uint32_t t = (uint32_t)e->d1 << 16 | (uint32_t)e->d2 << 8 | e->d3;
            if (t > 0) {
                tempo = t;
            }
        }
    }
    if (end_tick < last) {
        end_tick = last;
    }
    us += (uint64_t)(end_tick - last) * tempo / ppqn;
    merged[total] = (mev_t){
        .tick = end_tick,
        .status = MEV_TEMPO,
        .d1 = (uint8_t)(tempo >> 16),
        .d2 = (uint8_t)(tempo >> 8),
        .d3 = (uint8_t)tempo,
    };

    song->events = merged;
    song->count = total + 1;
    song->ppqn = ppqn;
    song->format = fmt;
    song->tracks = (uint16_t)nt;
    song->end_tick = end_tick;
    song->duration_ms = (uint32_t)(us / 1000);
    return ESP_OK;
}

void smf_free(smf_song_t *song)
{
    free(song->events);
    memset(song, 0, sizeof(*song));
}
