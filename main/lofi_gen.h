#pragma once

// Generative lo-fi hip hop composer. Plain C, no ESP-IDF calls. The stream is an endless chain of
// complete pieces; a piece is fully determined by its number (0..9999) and
// the next number follows deterministically from the current one, so a run
// is reproducible from its first piece number. Events are pulled in tick
// order at LOFI_PPQN; tempo changes are MEV_TEMPO events in the stream.

#include <stdbool.h>
#include <stdint.h>

#include "midi_event.h"

#define LOFI_PPQN 480
#define LOFI_PIECES 10000   // piece numbers are 0..LOFI_PIECES-1

typedef enum {
    LOFI_SEC_INTRO = 0,
    LOFI_SEC_GROOVE,
    LOFI_SEC_HOOK,
    LOFI_SEC_BREAK,
    LOFI_SEC_OUTRO,
} lofi_section_t;

typedef struct {
    uint32_t piece;          // number of the piece currently being generated
    uint32_t section;        // index of the newest generated section (0 = first intro)
    uint32_t bar;            // first bar of that section
    lofi_section_t type;
    int key;                 // tonic pitch class of that section, 0 = C
    int progression;         // 0-based index in the bank (the break uses break_progression)
    int break_progression;   // 0-based
    int key_lift;            // semitones the final hook and outro are lifted
    int rhodes_rhythm;       // 0-based
    int kick_pattern;        // 0-based
    int hat_pattern;         // 0-based
    int bass_figure;         // 0-based
    int motif;               // 0-based melody template of the piece's call phrase
    int sound;               // 0-based lead sound of the piece
    int bpm;                 // the piece's tempo before the tempo control
    int peak_notes;          // most notes sounding at once so far
    uint32_t shortened;      // Rhodes notes cut early to stay in the budget
    uint32_t dropped;        // notes not played because the budget was full
    uint32_t overflow;       // events lost to a full internal queue (should stay 0)
} lofi_status_t;

typedef struct lofi lofi_t;

// first_piece: 0..LOFI_PIECES-1 (larger values are taken modulo LOFI_PIECES)
lofi_t *lofi_create(uint32_t first_piece);
void lofi_destroy(lofi_t *g);

// Tempo of the piece being generated
int lofi_bpm(const lofi_t *g);

// Next events in tick order with tick <= horizon, at most `max`. Generates
// bars as needed, never far beyond the horizon. Channel setup (bend range,
// volume, pan, expression, reverb, programs, tempo) comes first at tick 0.
int lofi_pull(lofi_t *g, uint32_t horizon, mev_t *out, int max);

// Extra messages for Stop: pitch bend to center on ch1, CC11 back to 127
int lofi_stop_msgs(midi_msg_t *out, int max);

void lofi_status(const lofi_t *g, lofi_status_t *st);

const char *lofi_section_name(lofi_section_t type);
const char *lofi_key_name(int pitch_class);
