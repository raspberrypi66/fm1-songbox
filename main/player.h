#pragma once

// MIDI player: one task, paced by a 1 ms esp_timer. Pulls timed events from a
// source (a parsed SMF song or the lo-fi generator), applies tempo scale and
// transpose at send time, and hands the messages to midi_out_send().

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "midi_event.h"

typedef struct {
    uint16_t ppqn;
    uint32_t tempo_us;   // starting tempo, us per quarter note
    // Next events in tick order, all with tick <= horizon, at most `max`.
    // Returns the count. Set *ended once no more events will ever come.
    int (*pull)(void *ctx, uint32_t horizon, mev_t *out, int max, bool *ended);
    // Optional: extra messages sent after the standard all-off on stop
    int (*stop_msgs)(void *ctx, midi_msg_t *out, int max);
    // Optional: frees ctx; called by the player once the source is done
    void (*release)(void *ctx);
    void *ctx;
} player_source_t;

// Called from the player task when a source ends by itself (song end)
typedef void (*player_end_cb_t)(void);

esp_err_t player_init(player_end_cb_t on_end);

// Stops whatever plays, then plays `src` (copied) from the start. The player
// owns src->ctx from here and frees it through src->release, also when this
// fails (except ESP_ERR_INVALID_ARG, where nothing is taken).
esp_err_t player_start(const player_source_t *src);

// All Sound Off, Reset All Controllers and All Notes Off on all 16 channels,
// then the source's own stop messages. No-op when stopped.
void player_stop(void);

bool player_is_playing(void);

// Live changes, any task. Percent 50..150, semitones -12..+12 (clamped)
void player_set_tempo_scale(int percent);
void player_set_transpose(int semitones);
