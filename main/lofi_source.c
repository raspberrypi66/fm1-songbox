#include "lofi_source.h"

#include <stdlib.h>

#include "esp_log.h"
#include "lofi_gen.h"

static const char *TAG = "lofi";

typedef struct {
    lofi_t *gen;
    void (*on_piece)(uint32_t piece);
    uint32_t logged;      // last logged section + 1 (0 = none yet)
    bool piece_seen;      // a piece has been reported
    uint32_t piece;       // the reported piece
    uint8_t types_seen;   // bit per lofi_section_t
} lofi_src_t;

static int src_pull(void *ctx, uint32_t horizon, mev_t *out, int max, bool *ended)
{
    lofi_src_t *s = ctx;
    int n = lofi_pull(s->gen, horizon, out, max);

    lofi_status_t st;
    lofi_status(s->gen, &st);
    if (!s->piece_seen || st.piece != s->piece) {
        s->piece_seen = true;
        s->piece = st.piece;
        ESP_LOGI(TAG, "piece %lu: key %s, %d BPM, progression %d, break progression %d, lift +%d, motif %d, sound %d",
                 (unsigned long)st.piece, lofi_key_name(st.key), st.bpm, st.progression + 1,
                 st.break_progression + 1, st.key_lift, st.motif + 1, st.sound + 1);
        if (s->on_piece) {
            s->on_piece(st.piece);
        }
    }
    if (st.section + 1 != s->logged) {
        s->logged = st.section + 1;
        s->types_seen |= (uint8_t)(1u << st.type);
        ESP_LOGI(TAG, "bar %lu: piece %lu %s, key %s, Rhodes rhythm %d, kick %d, hats %d, bass %d",
                 (unsigned long)st.bar + 1, (unsigned long)st.piece, lofi_section_name(st.type),
                 lofi_key_name(st.key), st.rhodes_rhythm + 1, st.kick_pattern + 1, st.hat_pattern + 1,
                 st.bass_figure + 1);
    }
    *ended = false;   // endless chain of pieces
    return n;
}

static int src_stop_msgs(void *ctx, midi_msg_t *out, int max)
{
    (void)ctx;
    return lofi_stop_msgs(out, max);
}

static void src_release(void *ctx)
{
    lofi_src_t *s = ctx;
    lofi_status_t st;
    lofi_status(s->gen, &st);
    ESP_LOGI(TAG, "stopped in piece %lu section %lu: peak %d notes (budget 16) %s, %lu Rhodes notes shortened, %lu dropped, %lu lost",
             (unsigned long)st.piece, (unsigned long)st.section + 1, st.peak_notes,
             st.peak_notes <= 16 ? "OK" : "OVER", (unsigned long)st.shortened, (unsigned long)st.dropped,
             (unsigned long)st.overflow);
    ESP_LOGI(TAG, "section types generated:%s%s%s%s%s%s", (s->types_seen & 1) ? " intro" : "",
             (s->types_seen & 2) ? " groove" : "", (s->types_seen & 4) ? " hook" : "",
             (s->types_seen & 8) ? " break" : "", (s->types_seen & 16) ? " outro" : "",
             s->types_seen == 0x1F ? " (all)" : "");
    lofi_destroy(s->gen);
    free(s);
}

esp_err_t lofi_source_create(uint32_t first_piece, void (*on_piece)(uint32_t piece), player_source_t *src)
{
    lofi_src_t *s = calloc(1, sizeof(*s));
    if (!s) {
        return ESP_ERR_NO_MEM;
    }
    s->gen = lofi_create(first_piece);
    if (!s->gen) {
        free(s);
        return ESP_ERR_NO_MEM;
    }
    s->on_piece = on_piece;

    int bpm = lofi_bpm(s->gen);
    *src = (player_source_t){
        .ppqn = LOFI_PPQN,
        .tempo_us = 60000000u / (uint32_t)bpm,
        .pull = src_pull,
        .stop_msgs = src_stop_msgs,
        .release = src_release,
        .ctx = s,
    };
    ESP_LOGI(TAG, "start: first piece %lu, %d BPM", (unsigned long)first_piece % LOFI_PIECES, bpm);
    return ESP_OK;
}
