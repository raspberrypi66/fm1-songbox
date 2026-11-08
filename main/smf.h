#pragma once

// Standard MIDI File loader. Reads the whole file, parses format 0 and 1
// (PPQN timebase only) and merges all tracks into one tick-ordered event array
// in PSRAM: channel messages as they are, Set Tempo as MEV_TEMPO. SysEx and
// the other meta events are skipped. The last entry is a MEV_TEMPO marker at
// the End of Track tick, so playback ends where the file ends.

#include <stdint.h>

#include "esp_err.h"
#include "midi_event.h"

typedef struct {
    mev_t *events;          // PSRAM, tick order
    uint32_t count;
    uint16_t ppqn;
    uint16_t format;        // 0 or 1
    uint16_t tracks;
    uint32_t end_tick;      // latest End of Track
    uint32_t duration_ms;   // at the file's own tempo map
} smf_song_t;

// ESP_ERR_NOT_SUPPORTED: SMPTE timebase or format 2.
// ESP_ERR_INVALID_RESPONSE: not a MIDI file or broken. ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND.
esp_err_t smf_load(const char *path, smf_song_t *song);

// Header check only (first 14 bytes): same errors as smf_load
esp_err_t smf_check(const char *path);

void smf_free(smf_song_t *song);
