#include "smf_source.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "smf.h"

static const char *TAG = "smf";

typedef struct {
    smf_song_t song;
    uint32_t next;
} smf_src_t;

static int src_pull(void *ctx, uint32_t horizon, mev_t *out, int max, bool *ended)
{
    smf_src_t *s = ctx;
    int n = 0;
    while (s->next < s->song.count && n < max && s->song.events[s->next].tick <= horizon) {
        out[n++] = s->song.events[s->next++];
    }
    *ended = s->next >= s->song.count;
    return n;
}

static void src_release(void *ctx)
{
    smf_src_t *s = ctx;
    smf_free(&s->song);
    free(s);
}

esp_err_t smf_source_create(const char *path, player_source_t *src)
{
    smf_src_t *s = calloc(1, sizeof(*s));
    if (!s) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = smf_load(path, &s->song);
    if (err != ESP_OK) {
        free(s);
        return err;
    }

    const char *name = strrchr(path, '/');
    uint32_t sec = s->song.duration_ms / 1000;
    ESP_LOGI(TAG, "%s: format %u, %u tracks, PPQN %u, %lu events, %lu:%02lu",
             name ? name + 1 : path, s->song.format, s->song.tracks, s->song.ppqn,
             (unsigned long)s->song.count - 1, (unsigned long)sec / 60, (unsigned long)sec % 60);

    *src = (player_source_t){
        .ppqn = s->song.ppqn,
        .tempo_us = 500000,   // 120 BPM until the file's first Set Tempo
        .pull = src_pull,
        .release = src_release,
        .ctx = s,
    };
    return ESP_OK;
}
