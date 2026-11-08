// Generative lo-fi hip hop composer.
//
// The stream is a chain of complete pieces. A piece is planned from its
// number alone (tempo, key, progressions, key lift, motif, starting patterns)
// and plays a fixed form with an arc: intro, grooves and hooks, a break on a
// contrasting progression, a final lifted hook, an outro with a ritardando
// and a final chord, one silent bar, then the next piece. Tick time is
// continuous across pieces; tempo changes are MEV_TEMPO events in the stream.
//
// How it runs: lofi_pull() generates whole bars into a pending queue until it
// is a little past the requested horizon, sorts the queue, and hands out the
// events up to the horizon through an output filter. The filter keeps the
// stream clean: a note always ends before the same pitch starts again on its
// channel, and no more than POLY_BUDGET notes sound at once (old Rhodes notes
// are cut first, then new notes are dropped).
//
// Section settings are planned one section ahead (nxt), so the last bar of a
// section can already anticipate the next section's chord, fill and dynamics.
//
// Musical data lives in tables (progressions, voicings, Rhodes comping
// patterns, kick/hat patterns, bass figures, melody templates, form). The
// random generator only chooses among those options.

#include "lofi_gen.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

// ---- Time grid ----
#define PPQN          LOFI_PPQN
#define BEAT          PPQN
#define BAR           (4 * PPQN)
#define S16           (PPQN / 4)
#define START_TICK    (PPQN / 2)    // channel setup goes out at tick 0, music starts here
#define GEN_MARGIN    80            // generate past the horizon by this much (covers early events)

// ---- Limits ----
#define MAX_PENDING   1024
#define POLY_BUDGET   16
#define STEAL_MIN_AGE 60u           // never cut a Rhodes note this young (the chord being rolled)

// ---- Feel (ticks unless noted). The BLE link adds 0..15 ms on top, so there
// is no random timing here at all beyond +-1 tick on the keys: the pocket
// comes from the swing and from fixed lags. ----
#define SWING          20           // off-16ths late by 0.042 beat (about 58 % swing)
#define HUM_KEYS       1            // +- jitter on Rhodes and melody starts
#define HUM_VEL        5            // +- velocity on keys, bass
#define HUM_VEL_MEL    4
#define HUM_VEL_DRUM   3            // +- velocity on kick and snare
#define HUM_VEL_HAT    4
#define RHODES_LAG     6            // Rhodes behind the beat
#define SNARE_LAG      8            // snare / side stick behind the beat
#define MEL_LAG        2
#define ROLL_MS_MIN    8            // Rhodes chord roll, ms per note
#define ROLL_MS_MAX    12
#define ROLL_FINAL_MS  34           // the piece's last chord
#define GAP            15           // a note ends this long before the next one on its part

#define BEND_CENTER   8192

// ---- Ritardando in the outro: tempo falls to this percentage ----
#define RIT_END_PCT   72

// ---- Channels (0-based) and sounds ----
enum { CH_RHODES = 0, CH_BASS = 1, CH_PAD = 2, CH_MEL = 3, CH_LAYER = 4, CH_DRUMS = 9 };
enum { DR_KICK = 36, DR_STICK = 37, DR_SNARE = 38, DR_HAT = 42, DR_OPEN_HAT = 46 };
#define PROG_RHODES 4    // Electric Piano 1
#define PROG_BASS   33   // Electric Bass (finger)
#define PROG_PAD    49   // String Ensemble 2 (slow strings)
#define DRUM_LEN    60

// Lead sounds, one per piece: program, base velocity, channel volume (CC7
// goes out with the program change at the piece start) and the program that
// doubles it an octave below in the final hook. Chosen for a small GM
// wavetable: EP2 is the lo-fi "DX keys" lead, flute and vibes are the safe
// alternatives.
static const struct {
    uint8_t prog, vel, vol, layer;
} k_mel_sounds[] = {
    { 5, 88, 92, 11 },     // Electric Piano 2, doubled by Vibraphone
    { 73, 96, 100, 5 },    // Flute, doubled by Electric Piano 2
    { 11, 86, 96, 5 },     // Vibraphone, doubled by Electric Piano 2
};
#define MEL_SOUND_COUNT ((int)(sizeof(k_mel_sounds) / sizeof(k_mel_sounds[0])))
#define LAYER_VOL     64
#define LAYER_VEL_ADJ (-14)

// ---- Registers ----
#define RHODES_LO 52   // E3
#define RHODES_HI 74   // D5
#define PAD_LO    55   // G3
#define PAD_HI    74   // D5
#define MEL_LO    72   // C5: the lead sits above the Rhodes top voice (D5 at most)
#define MEL_HI    89   // F6
#define BASS_LO   31   // G1: roots sit in G1..F#2
#define BASS_HI   47   // B2: fifths and octaves above a root stay below this

// ---- Harmony ----

typedef enum {
    Q_MAJ9,       // Imaj9, IVmaj9
    Q_MIN9,       // ii9 (dorian: the 13th is in the key)
    Q_MIN9_AEOL,  // vi9 (aeolian: keep the 5th, the 13th would be outside the key)
    Q_MIN7,       // iii7
    Q_DOM13,      // V13
    Q_DOM13SUS,   // V13sus
    Q_HALFDIM,    // vii-7b5
    Q_DOM7B9,     // III7 (V7b9 of vi)
    Q_COUNT
} quality_t;

typedef struct {
    uint8_t root;   // semitones above the tonic
    uint8_t q;      // quality_t
} chord_t;

typedef struct {
    uint8_t n;      // chords in the bar: 1, or 2 (second one from beat 3)
    chord_t c[2];
} bar_t;

// Rootless 4-note voicings, intervals above the chord root: 3rd, 5th or 13th,
// 7th, 9th. Where the 9th is outside the key, the 11th or b9 replaces it.
typedef struct {
    uint8_t count;
    uint8_t iv[2][4];
} voicing_set_t;

static const voicing_set_t k_voicings[Q_COUNT] = {
    [Q_MAJ9]      = { 2, { { 4, 7, 11, 14 }, { 4, 9, 11, 14 } } },
    [Q_MIN9]      = { 2, { { 3, 7, 10, 14 }, { 3, 9, 10, 14 } } },
    [Q_MIN9_AEOL] = { 1, { { 3, 7, 10, 14 } } },
    [Q_MIN7]      = { 1, { { 3, 7, 10, 17 } } },   // 11 instead of the b9
    [Q_DOM13]     = { 2, { { 4, 9, 10, 14 }, { 4, 7, 10, 14 } } },
    [Q_DOM13SUS]  = { 1, { { 5, 9, 10, 14 } } },   // the 4th replaces the 3rd
    [Q_HALFDIM]   = { 1, { { 3, 6, 10, 17 } } },   // b3 b5 b7 11
    [Q_DOM7B9]    = { 1, { { 4, 7, 10, 13 } } },
};

// Chord tones (root, 3rd, 5th, 7th): bass, pad
static const uint8_t k_tones[Q_COUNT][4] = {
    [Q_MAJ9]      = { 0, 4, 7, 11 },
    [Q_MIN9]      = { 0, 3, 7, 10 },
    [Q_MIN9_AEOL] = { 0, 3, 7, 10 },
    [Q_MIN7]      = { 0, 3, 7, 10 },
    [Q_DOM13]     = { 0, 4, 7, 10 },
    [Q_DOM13SUS]  = { 0, 5, 7, 10 },
    [Q_HALFDIM]   = { 0, 3, 6, 10 },
    [Q_DOM7B9]    = { 0, 4, 7, 10 },
};

// Melody target degrees per quality: semitones above the root for degree
// 1 root, 2 9th, 3 3rd, 4 11th, 5 5th, 6 13th, 7 7th (index 0 unused). Where a
// colour tone would be outside the key or clash with the Rhodes voicing it is
// replaced by the nearest chord tone (iii7: b9 -> b3, b13 -> 5th; vi9: b13 ->
// 5th; sus: 3rd -> 4th; vii-7b5: 13th -> 11th).
static const int8_t k_degree_iv[Q_COUNT][8] = {
    [Q_MAJ9]      = { 0, 0, 2, 4, 5, 7, 9, 11 },
    [Q_MIN9]      = { 0, 0, 2, 3, 5, 7, 9, 10 },
    [Q_MIN9_AEOL] = { 0, 0, 2, 3, 5, 7, 7, 10 },
    [Q_MIN7]      = { 0, 0, 3, 3, 5, 7, 7, 10 },
    [Q_DOM13]     = { 0, 0, 2, 4, 5, 7, 9, 10 },
    [Q_DOM13SUS]  = { 0, 0, 2, 5, 5, 7, 9, 10 },
    [Q_HALFDIM]   = { 0, 0, 2, 3, 5, 6, 5, 10 },
    [Q_DOM7B9]    = { 0, 0, 1, 4, 5, 7, 8, 10 },
};

// The next chord tone above a degree (the answer phrase moves its targets up
// one chord tone): 1 -> 3, 3 -> 5, 5 -> 7, 7 -> 9; colour tones go to the
// chord tone above them
static const uint8_t k_up_tone[8] = { 0, 3, 3, 5, 5, 7, 7, 2 };

#define C1(r, q)           { 1, { { r, q } } }
#define C2(r1, q1, r2, q2) { 2, { { r1, q1 }, { r2, q2 } } }

static const bar_t k_progs[][4] = {
    // ii9 | V13 | Imaj9 | vi9
    { C1(2, Q_MIN9), C1(7, Q_DOM13), C1(0, Q_MAJ9), C1(9, Q_MIN9_AEOL) },
    // Imaj9 | iii7 | vi9 | ii9 V13
    { C1(0, Q_MAJ9), C1(4, Q_MIN7), C1(9, Q_MIN9_AEOL), C2(2, Q_MIN9, 7, Q_DOM13) },
    // IVmaj9 | iii7 | ii9 | Imaj9
    { C1(5, Q_MAJ9), C1(4, Q_MIN7), C1(2, Q_MIN9), C1(0, Q_MAJ9) },
    // vi9 | ii9 | V13sus | Imaj9
    { C1(9, Q_MIN9_AEOL), C1(2, Q_MIN9), C1(7, Q_DOM13SUS), C1(0, Q_MAJ9) },
    // Imaj9 | vii-7b5 III7 | vi9 | V13
    { C1(0, Q_MAJ9), C2(11, Q_HALFDIM, 4, Q_DOM7B9), C1(9, Q_MIN9_AEOL), C1(7, Q_DOM13) },
    // ii9 | iii7 | IVmaj9 | V13sus V13
    { C1(2, Q_MIN9), C1(4, Q_MIN7), C1(5, Q_MAJ9), C2(7, Q_DOM13SUS, 7, Q_DOM13) },
    // IVmaj9 | V13 | iii7 | vi9
    { C1(5, Q_MAJ9), C1(7, Q_DOM13), C1(4, Q_MIN7), C1(9, Q_MIN9_AEOL) },
    // vi9 | IVmaj9 | Imaj9 | V13sus
    { C1(9, Q_MIN9_AEOL), C1(5, Q_MAJ9), C1(0, Q_MAJ9), C1(7, Q_DOM13SUS) },
};
#define PROG_COUNT ((int)(sizeof(k_progs) / sizeof(k_progs[0])))

// The outro sits on Imaj9
static const bar_t k_outro_bar = C1(0, Q_MAJ9);

// Db, Eb, F, Ab, C, A
static const uint8_t k_keys[] = { 1, 3, 5, 8, 0, 9 };
#define KEY_COUNT ((int)(sizeof(k_keys) / sizeof(k_keys[0])))

// ---- Form: one piece ----
//
// Sections in order with their length, velocity offset (the dynamic arc)
// and role. The outro is 4 bars of music plus one silent bar before the next
// piece. Everything from the final hook on is played in the lifted key.

typedef struct {
    uint8_t type;
    uint8_t bars;
    int8_t dyn;
    bool final;    // the final hook: lifted key, full lead with doubling
} form_t;

static const form_t k_form[] = {
    { LOFI_SEC_INTRO, 8, -14, false },
    { LOFI_SEC_GROOVE, 8, -6, false },
    { LOFI_SEC_HOOK, 8, 0, false },
    { LOFI_SEC_GROOVE, 8, -6, false },
    { LOFI_SEC_HOOK, 8, 0, false },
    { LOFI_SEC_BREAK, 8, -10, false },
    { LOFI_SEC_HOOK, 8, 6, true },
    { LOFI_SEC_OUTRO, 5, -12, false },
};
#define FORM_LEN   ((int)(sizeof(k_form) / sizeof(k_form[0])))
#define FINAL_IDX  6          // first section in the lifted key
#define OUTRO_BARS 4          // bars of music in the outro; the 5th is the gap
#define PIECE_BARS 61         // sum of k_form[].bars

// ---- Rhodes comping patterns ----
//
// One bar on the 16th grid: hits (start, end, velocity). A pattern with
// `push` plays the next bar's first chord on this bar's 4& and ties it over.
// Bars with two chords always get a hit on beat 3.

typedef struct {
    int8_t s16, e16;
    uint8_t vel;
} rhit_t;

typedef struct {
    uint8_t n;
    bool push;
    rhit_t hit[3];
} rpat_t;

static const rpat_t k_rhodes_pats[] = {
    { 1, false, { { 0, 16, 66 } } },                                // 0 sustained whole bar
    { 2, false, { { 0, 6, 70 }, { 6, 16, 62 } } },                  // 1 "1 .. 2&"
    { 2, false, { { 0, 10, 70 }, { 10, 16, 64 } } },                // 2 "1 .. 3&"
    { 2, true,  { { 0, 8, 70 }, { 8, 13, 62 } } },                  // 3 "1, 3, push on 4&"
    { 3, false, { { 0, 3, 72 }, { 6, 8, 58 }, { 10, 16, 66 } } },   // 4 stabs "1 . 2& . 3&"
    { 3, false, { { 0, 4, 70 }, { 6, 8, 56 }, { 8, 16, 64 } } },    // 5 "1 . 2& 3"
};
#define PUSH_S16 14   // the pushed chord starts on 4&

// Which Rhodes patterns each section type may use (-1 ends the list)
static const int8_t k_rhythm_allowed[5][6] = {
    [LOFI_SEC_INTRO]  = { 0, 2, -1 },
    [LOFI_SEC_GROOVE] = { 1, 2, 3, 4, 5, -1 },
    [LOFI_SEC_HOOK]   = { 1, 2, 3, -1 },
    [LOFI_SEC_BREAK]  = { 0, 1, -1 },
    [LOFI_SEC_OUTRO]  = { 0, -1 },
};

// ---- Drum patterns ----

// Kick positions in 16ths for bar 1 and bar 2 (-1 ends the list) and the
// position of the ghost snare played on every other bar (-1 = none)
typedef struct {
    int8_t k[2][4];
    int8_t ghost;
} kick_t;

static const kick_t k_kicks[] = {
    { { { 0, 10, -1, -1 }, { 0, 7, 10, -1 } }, 15 },   // 1 3&   | 1 2a 3&
    { { { 0, 6, -1, -1 }, { 0, 6, 11, -1 } }, 7 },     // 1 2&   | 1 2& 3a
    { { { 0, 3, 10, -1 }, { 0, 10, 14, -1 } }, -1 },   // 1 1a 3& | 1 3& 4&
    { { { 0, 7, -1, -1 }, { 0, 6, 10, -1 } }, 15 },    // 1 2a   | 1 2& 3&
};
#define KICK_COUNT ((int)(sizeof(k_kicks) / sizeof(k_kicks[0])))

// Closed hat velocity per 16th, 0 = no hat
static const uint8_t k_hats[][16] = {
    { 72, 0, 50, 0, 70, 0, 50, 0, 72, 0, 50, 0, 70, 0, 52, 0 },     // 0 straight 8ths
    { 72, 0, 50, 0, 70, 0, 50, 36, 72, 0, 50, 0, 70, 0, 52, 38 },   // 1 8ths with swung pickups on 2a and 4a
    { 58, 0, 64, 0, 56, 0, 66, 0, 58, 0, 64, 0, 56, 0, 68, 0 },     // 2 offbeats leaning
    { 50, 0, 0, 0, 48, 0, 0, 0, 50, 0, 0, 0, 48, 0, 0, 0 },         // 3 soft quarters (break)
};
#define HAT_BREAK 3

// ---- Bass figures: interval above the chord root for the 1st, 2nd and 3rd
// kick of a bar (the 1st is always the root) ----
static const int8_t k_bass_figs[][3] = {
    { 0, 0, 0 },     // roots only
    { 0, 7, 0 },     // fifth on the second kick
    { 0, 0, 12 },    // octave on the third
    { 0, 12, 7 },    // octave, then fifth
};
#define BASS_FIG_COUNT ((int)(sizeof(k_bass_figs) / sizeof(k_bass_figs[0])))

// ---- Melody motif templates ----
//
// Two bars on the 16th grid (0..31), written like a topline: each note is a
// chord target (a degree of the chord sounding under it, nearest to the
// previous note, optionally forced up or down), a diatonic step from the
// previous note, a chromatic or diatonic approach into the next note, or a
// repeat. Each note carries its velocity offset, so the phrase has a shape.
// Call templates end on a 9th or 7th (a question), answer templates on the
// root or 3rd (the answer). Every template has rests and a long last note.

enum { K_T, K_S, K_A, K_R };

typedef struct {
    uint8_t start, len;
    uint8_t kind;
    int8_t val;    // K_T: degree 1..7, K_S: steps, K_A: -1 chromatic below / +1 diatonic above the next note
    int8_t dir;    // K_T only: 0 nearest, +1 above the previous note, -1 below
    int8_t vel;    // velocity offset
} tnote_t;

typedef struct {
    uint8_t n;
    tnote_t note[12];
} template_t;

#define NT(s, l, deg, dir, v) { s, l, K_T, deg, dir, v }
#define NS(s, l, steps, v)    { s, l, K_S, steps, 0, v }
#define NA(s, l, how, v)      { s, l, K_A, how, 0, v }
#define NR(s, l, v)           { s, l, K_R, 0, 0, v }

static const template_t k_calls[] = {
    // 0 "sigh": 5th, step down, lean on the 3rd; chromatic pickup into the 9th of the next chord, falls to the 7th
    { 7, { NT(2, 2, 5, 0, -4), NS(4, 2, -1, -6), NT(6, 5, 3, 0, 2), NA(14, 2, -1, -8),
           NT(16, 6, 2, 0, 4), NS(24, 2, -1, -4), NT(26, 5, 7, -1, -2) } },
    // 1 "bounce": a repeated-note cell on the 3rd, up to the 5th; the same cell over the next chord, ends on the 9th
    { 10, { NT(0, 2, 3, 0, 0), NR(2, 1, -10), NR(3, 3, -6), NS(8, 2, 1, -2), NT(10, 4, 5, 0, 2),
            NT(16, 2, 3, 0, 0), NR(18, 1, -10), NR(19, 3, -6), NS(24, 2, -1, -2), NT(26, 5, 2, 0, 2) } },
    // 2 "run up": scale run from the root on 2& up to the 9th on bar 2 beat 1 (the peak), tail down to the 7th
    { 9, { NT(6, 2, 1, 0, -6), NS(8, 2, 1, -4), NS(10, 2, 1, -2), NS(12, 2, 1, 0), NS(14, 2, 1, 2),
           NT(16, 6, 2, 0, 4), NS(24, 2, -1, -4), NS(26, 2, -1, -6), NT(28, 3, 7, -1, -2) } },
    // 3 "lazy dotted": dotted 8th + 16th pairs between the 5th, 3rd and 9th; same shape over the next chord
    { 10, { NT(0, 3, 5, 0, 0), NS(3, 1, -1, -10), NT(4, 3, 3, 0, -2), NS(7, 1, 1, -10), NT(8, 6, 2, 0, 2),
            NT(16, 3, 3, 0, 0), NS(19, 1, -1, -10), NT(20, 3, 1, 0, -2), NS(23, 1, 1, -10), NT(24, 6, 7, 0, 2) } },
    // 4 "soul leap": hold the 3rd, leap up to the 13th on 2&, settle on the 5th; bar 2 walks down to the 9th
    { 6, { NT(0, 4, 3, 0, 0), NT(6, 2, 6, 1, 2), NT(8, 6, 5, -1, 4),
           NS(18, 2, -1, -4), NS(20, 2, -1, -6), NT(22, 8, 2, 0, -2) } },
    // 5 "cell echo": three-note cell 9th-step-5th, silence, the cell again over the next chord ending on its 3rd
    { 6, { NT(0, 2, 2, 0, 0), NS(2, 2, 1, -4), NT(4, 6, 5, 0, 2),
           NT(16, 2, 2, 0, 0), NS(18, 2, 1, -4), NT(20, 6, 3, 0, 2) } },
    // 6 "swung": chromatic pickup on 1a into the 3rd, swung 16th neighbours, lands on the 7th of the next chord
    { 9, { NA(3, 1, -1, -8), NT(4, 2, 3, 0, 0), NS(7, 1, -1, -8), NT(8, 4, 5, 0, 2), NS(12, 2, -1, -4),
           NS(14, 2, -1, -6), NT(16, 6, 7, 0, 2), NS(24, 2, 1, -4), NT(26, 5, 2, 0, 0) } },
    // 7 "falling": starts high on the 9th and steps down to the 3rd; bar 2 the same from the 9th down to the 7th
    { 8, { NT(2, 2, 2, 1, 0), NS(4, 2, -1, -4), NS(6, 2, -1, -6), NT(8, 6, 3, 0, -2),
           NT(18, 2, 2, 0, 0), NS(20, 2, -1, -4), NS(22, 2, -1, -6), NT(24, 6, 7, 0, -2) } },
};
#define CALL_COUNT ((int)(sizeof(k_calls) / sizeof(k_calls[0])))

// Fallback answers (1 in 4 pieces); usually the answer is developed from the call
static const template_t k_answers[] = {
    // 0: 3rd, step, rest on the root; 9th with a chromatic pickup into the 3rd
    { 6, { NT(2, 2, 3, 0, 0), NS(4, 2, -1, -4), NT(6, 6, 1, 0, -2),
           NT(18, 2, 2, 0, -2), NA(20, 2, -1, -8), NT(22, 8, 3, 0, 0) } },
    // 1: repeated 5th, down to the 3rd; 9th steps down to the root
    { 7, { NT(0, 2, 5, 0, 0), NR(2, 2, -8), NS(4, 2, -1, -4), NT(6, 6, 3, 0, 0),
           NT(16, 2, 2, 0, -2), NS(18, 2, -1, -6), NT(20, 8, 1, 0, -2) } },
    // 2: late entry on beat 2, steps down to the 5th; bar 2 steps back up to the 3rd
    { 7, { NT(4, 2, 2, 0, 0), NS(6, 2, -1, -4), NS(8, 2, -1, -4), NT(10, 6, 5, -1, 2),
           NS(20, 2, 1, -4), NS(22, 2, 1, -2), NT(24, 6, 3, 0, 0) } },
    // 3: dotted cell 3rd-root; dotted cell 5th-3rd (echoes call 3)
    { 6, { NT(0, 3, 3, 0, 0), NS(3, 1, -1, -10), NT(4, 6, 1, 0, -2),
           NT(16, 3, 5, -1, 0), NS(19, 1, -1, -10), NT(20, 8, 3, 0, -2) } },
    // 4: sparse: pickup into the 3rd on beat 3, then 9th down to the root
    { 5, { NA(6, 2, -1, -8), NT(8, 6, 3, 0, 2),
           NT(20, 2, 2, 0, -2), NS(22, 2, -1, -6), NT(24, 6, 1, 0, -2) } },
};
#define ANSWER_COUNT ((int)(sizeof(k_answers) / sizeof(k_answers[0])))

// ---- Generator state ----

typedef struct {
    uint8_t call, answer;   // template indices into k_calls / k_answers
    uint8_t center;         // the call's first target is the one nearest this pitch
    uint8_t vary_deg;       // degree of the last note in the varied repeat
    bool vary_cut;          // varied repeat drops its second-to-last note
    uint8_t answer_end;     // degree the developed answer ends on (3 or 7)
    bool answer_from_bank;  // use k_answers instead of developing the call
    uint8_t final_deg;      // degree the hook comes to rest on (1 or 3)
    uint8_t sound;          // index into k_mel_sounds
} motif_t;

// Everything a piece is made of, planned from its number alone
typedef struct {
    uint32_t number;
    int bpm;
    uint8_t key;
    uint8_t prog, break_prog;
    uint8_t lift;           // semitones, applied from the final hook on
    motif_t motif;
    uint8_t rhythm, kick, hat, bass;   // patterns the intro starts with
    uint32_t rng_after;     // generator state to continue the piece with
} piece_t;

typedef struct {
    lofi_section_t type;
    uint8_t form_idx;
    uint8_t bars;
    int8_t dyn;
    bool final;
    uint32_t piece;         // piece number
    uint32_t piece_bar;     // first bar of the piece (progressions count from it)
    int bpm;
    uint8_t prog;
    uint8_t key;
    uint8_t rhythm;
    uint8_t kick;
    uint8_t hat;
    uint8_t bass;
    motif_t motif;
} sect_t;

typedef struct {
    mev_t ev;
    uint32_t seq;   // keeps insertion order among equal ticks
} pend_t;

struct lofi {
    uint32_t rng;

    piece_t piece, next_piece;
    sect_t cur, nxt;
    uint32_t section;        // index of cur
    uint32_t section_bar;    // first bar of cur
    uint32_t bar;            // next bar to generate
    uint32_t bar_tick;       // its start tick

    uint8_t rhodes_prev[4];  // last voicings, for voice leading
    uint8_t pad_prev[3];
    uint8_t pad_held[3];     // pad notes sounding now (pad_on)
    bool pad_on;
    bool anticipated;        // this bar's first Rhodes chord was already played at the end of the last bar
    int rhodes_expr;         // current CC11 levels
    int pad_expr;

    pend_t pend[MAX_PENDING];
    int npend;
    uint32_t seq;
    bool dirty;

    // Output filter
    uint8_t depth[16][128];      // notes generated and not yet ended, per pitch
    uint8_t sounding[16][128];   // a note is actually sounding
    uint32_t rhodes_on[128];     // start tick of each sounding Rhodes note
    int active;
    int peak;
    uint32_t shortened, dropped, overflow;
};

// ---- Random ----

static uint32_t rnd_next(lofi_t *g)
{
    uint32_t x = g->rng;   // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

static int rnd(lofi_t *g, int n)
{
    return n > 0 ? (int)(rnd_next(g) % (uint32_t)n) : 0;
}

static int rnd_range(lofi_t *g, int lo, int hi)
{
    return lo + rnd(g, hi - lo + 1);
}

static bool chance(lofi_t *g, int pct)
{
    return rnd(g, 100) < pct;
}

// A value in 0..n-1 other than cur
static int other(lofi_t *g, int cur, int n)
{
    int v = rnd(g, n - 1);
    return v >= cur ? v + 1 : v;
}

// A random entry of a -1 terminated list, preferring one other than `avoid`
static int pick_from(lofi_t *g, const int8_t *list, int avoid)
{
    int n = 0, cand[8], nc = 0;
    for (; list[n] >= 0; n++) {
        if (list[n] != avoid) {
            cand[nc++] = list[n];
        }
    }
    return nc > 0 ? cand[rnd(g, nc)] : list[rnd(g, n)];
}

static bool in_list(const int8_t *list, int v)
{
    for (int i = 0; list[i] >= 0; i++) {
        if (list[i] == v) {
            return true;
        }
    }
    return false;
}

// Integer hash mixing (splitmix-style finaliser)
static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static uint32_t piece_seed(uint32_t number)
{
    uint32_t x = mix32((number + 1) * 0x9E3779B9u);
    return x ? x : 1;
}

// The piece that follows `number` in the chain, never the same number twice in a row
static uint32_t next_piece_number(uint32_t number)
{
    uint32_t m = mix32(number * 0x85EBCA6Bu + 0xC2B2AE35u) % LOFI_PIECES;
    return m == number ? (number + 1) % LOFI_PIECES : m;
}

// ---- Small helpers ----

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int jitter(lofi_t *g, int amount)
{
    return amount > 0 ? rnd_range(g, -amount, amount) : 0;
}

static int hum_v(lofi_t *g, int vel, int amount)
{
    return clampi(vel + rnd_range(g, -amount, amount), 1, 127);
}

static uint32_t at(uint32_t t, int offset)
{
    int64_t v = (int64_t)t + offset;
    return v < 0 ? 0 : (uint32_t)v;
}

static int ms_ticks(const lofi_t *g, int ms)
{
    return ms * PPQN * g->cur.bpm / 60000;
}

// Tick of a 16th step from a bar start. Off-16ths (x.25, x.75) are swung late.
static uint32_t grid(uint32_t bar_tick, int s16)
{
    uint32_t t = bar_tick + (uint32_t)s16 * S16;
    return (s16 & 1) ? t + SWING : t;
}

static uint16_t major_mask(int key)
{
    static const uint8_t k_major[] = { 0, 2, 4, 5, 7, 9, 11 };
    uint16_t m = 0;
    for (int i = 0; i < 7; i++) {
        m |= (uint16_t)(1u << ((key + k_major[i]) % 12));
    }
    return m;
}

static uint16_t tone_mask(int root_pc, int q)
{
    uint16_t m = 0;
    for (int i = 0; i < 4; i++) {
        m |= (uint16_t)(1u << ((root_pc + k_tones[q][i]) % 12));
    }
    return m;
}

static bool in_mask(int pitch, uint16_t mask)
{
    return (mask >> (pitch % 12)) & 1;
}

// Nearest pitch in [lo, hi] whose pitch class is in mask (ties go down)
static int nearest_in_mask(int pitch, uint16_t mask, int lo, int hi)
{
    for (int d = 0; d <= 12; d++) {
        if (pitch - d >= lo && pitch - d <= hi && in_mask(pitch - d, mask)) {
            return pitch - d;
        }
        if (pitch + d >= lo && pitch + d <= hi && in_mask(pitch + d, mask)) {
            return pitch + d;
        }
    }
    return clampi(pitch, lo, hi);
}

// The chord degree (1..7) nearest to `from` inside the melody range; dir
// forces it above (+1) or below (-1) `from`. Falls back to nearest overall.
static int target_pitch(int root_pc, int q, int deg, int from, int dir)
{
    int pc = (root_pc + k_degree_iv[q][deg]) % 12;
    int best = -1;
    for (int p = MEL_LO; p <= MEL_HI; p++) {
        if (p % 12 != pc || (dir > 0 && p <= from) || (dir < 0 && p >= from)) {
            continue;
        }
        if (best < 0 || abs(p - from) < abs(best - from)) {
            best = p;
        }
    }
    return best >= 0 ? best : nearest_in_mask(from, (uint16_t)(1u << pc), MEL_LO, MEL_HI);
}

// `steps` notes of the key's major scale away from `from`, kept in range by
// octave. From a chromatic note the first step lands on the next scale tone.
static int scale_step(int key, int from, int steps)
{
    uint16_t m = major_mask(key);
    int p = from, n = abs(steps), d = steps > 0 ? 1 : -1;
    while (n > 0) {
        p += d;
        if (in_mask(p, m)) {
            n--;
        }
    }
    if (p > MEL_HI) {
        p -= 12;
    } else if (p < MEL_LO) {
        p += 12;
    }
    return p;
}

// Diatonic notes that rub against the chord when held: the 11th on major and
// dominant chords, the 3rd on a sus, the b9 and b13 on iii7, the b13 on vi9
static bool avoid_tone(int root_pc, int q, int pitch)
{
    int d = ((pitch - root_pc) % 12 + 12) % 12;
    switch (q) {
    case Q_MAJ9:
    case Q_DOM13:
    case Q_DOM7B9:
        return d == 5;
    case Q_DOM13SUS:
        return d == 4;
    case Q_MIN7:
        return d == 1 || d == 8;
    case Q_MIN9_AEOL:
        return d == 8;
    default:
        return false;
    }
}

// ---- Pending events ----

static void put(lofi_t *g, uint32_t tick, uint8_t status, uint8_t d1, uint8_t d2)
{
    if (g->npend >= MAX_PENDING) {
        g->overflow++;
        return;
    }
    pend_t *p = &g->pend[g->npend++];
    p->ev = (mev_t){ .tick = tick, .status = status, .d1 = d1, .d2 = d2 };
    p->seq = g->seq++;
    g->dirty = true;
}

static void note(lofi_t *g, int ch, uint32_t on, uint32_t off, int pitch, int vel)
{
    if (pitch < 0 || pitch > 127) {
        return;
    }
    if (off < on + 20) {
        off = on + 20;
    }
    put(g, on, (uint8_t)(0x90 | ch), (uint8_t)pitch, (uint8_t)clampi(vel, 1, 127));
    put(g, off, (uint8_t)(0x80 | ch), (uint8_t)pitch, 0);
}

static void cc(lofi_t *g, int ch, uint32_t tick, int num, int val)
{
    put(g, tick, (uint8_t)(0xB0 | ch), (uint8_t)num, (uint8_t)clampi(val, 0, 127));
}

// Tempo change for the player: bpm scaled by pct percent
static void put_tempo(lofi_t *g, uint32_t tick, int bpm, int pct)
{
    if (g->npend >= MAX_PENDING) {
        g->overflow++;
        return;
    }
    uint32_t us = 6000000000ull / (uint32_t)(bpm * pct);   // 60e6 * 100 / (bpm * pct)
    pend_t *p = &g->pend[g->npend++];
    p->ev = (mev_t){ .tick = tick, .status = MEV_TEMPO,
                     .d1 = (uint8_t)(us >> 16), .d2 = (uint8_t)(us >> 8), .d3 = (uint8_t)us };
    p->seq = g->seq++;
    g->dirty = true;
}

// Four CC11 steps across a bar, from the current level to `to`
static void ramp(lofi_t *g, int ch, uint32_t t0, int from, int to)
{
    for (int i = 0; i < 4; i++) {
        cc(g, ch, t0 + (uint32_t)i * BEAT, 11, from + (to - from) * (i + 1) / 4);
    }
}

// ---- Song structure lookups ----

// Settings for global bar b: the current section or the planned next one
static const sect_t *sect_of(const lofi_t *g, uint32_t b)
{
    return b < g->section_bar + g->cur.bars ? &g->cur : &g->nxt;
}

static const bar_t *bar_chords(const lofi_t *g, uint32_t b)
{
    const sect_t *s = sect_of(g, b);
    if (s->type == LOFI_SEC_OUTRO) {
        return &k_outro_bar;
    }
    return &k_progs[s->prog][(b - s->piece_bar) % 4];
}

static chord_t chord_at(const lofi_t *g, uint32_t b, int pos)
{
    const bar_t *bc = bar_chords(g, b);
    return (bc->n == 2 && pos >= BAR / 2) ? bc->c[1] : bc->c[0];
}

static int root_pc(const lofi_t *g, uint32_t b, chord_t c)
{
    return (sect_of(g, b)->key + c.root) % 12;
}

// CC11 levels per section: the arc on top of the velocities
static int rhodes_expr(const sect_t *s)
{
    switch (s->type) {
    case LOFI_SEC_INTRO:
        return 112;
    case LOFI_SEC_GROOVE:
        return 118;
    case LOFI_SEC_HOOK:
        return 127;
    case LOFI_SEC_BREAK:
        return 106;
    default:
        return 112;
    }
}

static int pad_expr(const sect_t *s)
{
    if (s->type == LOFI_SEC_HOOK) {
        return s->final ? 118 : 110;
    }
    return 100;
}

// ---- Voicing ----

// Place the pitch classes pcs[0..n-1] (n = 3 or 4) inside [lo, hi] as the
// voicing closest to prev: smallest total movement, lightly pulled toward the
// middle of the range so voice leading does not drift to an edge. Writes the
// voicing sorted low to high and returns its cost (INT_MAX if nothing fits).
static int place(lofi_t *g, const int *pcs, int n, int lo, int hi, const uint8_t *prev, uint8_t *out)
{
    int opts[4][3], nopt[4];
    for (int i = 0; i < n; i++) {
        nopt[i] = 0;
        for (int p = lo + ((pcs[i] - lo) % 12 + 12) % 12; p <= hi && nopt[i] < 3; p += 12) {
            opts[i][nopt[i]++] = p;
        }
        if (nopt[i] == 0) {
            return INT_MAX;
        }
    }

    int best = INT_MAX, ties = 0;
    int mid = (lo + hi) / 2;
    int idx[4] = { 0 };
    for (;;) {
        int v[4] = { 0 };
        for (int i = 0; i < n; i++) {
            v[i] = opts[i][idx[i]];
        }
        for (int i = 1; i < n; i++) {   // sort
            for (int j = i; j > 0 && v[j] < v[j - 1]; j--) {
                int t = v[j];
                v[j] = v[j - 1];
                v[j - 1] = t;
            }
        }
        bool ok = v[n - 1] - v[0] <= 14;            // within a hand span
        ok = ok && !(v[1] - v[0] == 1 && v[0] < 55); // no muddy low minor 2nd
        for (int i = 1; ok && i < n; i++) {
            ok = v[i] != v[i - 1];
        }
        if (ok) {
            int move = 0, sum = 0;
            for (int i = 0; i < n; i++) {
                move += abs(v[i] - prev[i]);
                sum += v[i];
            }
            int cost = 2 * move + abs(sum / n - mid);
            if (cost < best) {
                best = cost;
                ties = 1;
            } else if (cost == best) {
                ties++;
            }
            if (cost == best && rnd(g, ties) == 0) {
                for (int i = 0; i < n; i++) {
                    out[i] = (uint8_t)v[i];
                }
            }
        }
        int k = 0;
        while (k < n && ++idx[k] == nopt[k]) {
            idx[k] = 0;
            k++;
        }
        if (k == n) {
            break;
        }
    }
    return best;
}

static void voice_rhodes(lofi_t *g, int root, int q, uint8_t out[4])
{
    const voicing_set_t *vs = &k_voicings[q];
    memcpy(out, g->rhodes_prev, 4);
    int best = INT_MAX;
    for (int v = 0; v < vs->count; v++) {
        int pcs[4];
        uint8_t cand[4] = { 0 };
        for (int i = 0; i < 4; i++) {
            pcs[i] = (root + vs->iv[v][i]) % 12;
        }
        int cost = place(g, pcs, 4, RHODES_LO, RHODES_HI, g->rhodes_prev, cand);
        if (cost < best || (cost == best && cost != INT_MAX && chance(g, 50))) {
            best = cost;
            memcpy(out, cand, 4);
        }
    }
    memcpy(g->rhodes_prev, out, 4);
}

// ---- Melody ----

// The piece's one melodic idea: a call template, how its answer is made, the
// register of the first note, how the varied repeat and the final phrase end,
// and the lead sound
static void make_motif(lofi_t *g, motif_t *m)
{
    m->call = (uint8_t)rnd(g, CALL_COUNT);
    m->answer = (uint8_t)rnd(g, ANSWER_COUNT);
    m->answer_from_bank = chance(g, 25);
    m->answer_end = chance(g, 50) ? 3 : 7;
    m->center = (uint8_t)(77 + 2 * rnd(g, 3));   // F5, G5 or A5

    // The varied repeat ends on another chord degree than the call does
    static const uint8_t k_end_degs[] = { 1, 3, 5, 7 };
    const template_t *tp = &k_calls[m->call];
    int last_deg = tp->note[tp->n - 1].val;
    uint8_t deg;
    do {
        deg = k_end_degs[rnd(g, 4)];
    } while (deg == last_deg);
    m->vary_deg = deg;
    m->vary_cut = chance(g, 50);
    m->final_deg = chance(g, 50) ? 1 : 3;
    m->sound = (uint8_t)rnd(g, MEL_SOUND_COUNT);
}

enum { PH_CALL, PH_VARIED, PH_ANSWER, PH_FINAL };

// Each phrase of the 8-bar hook has its own register: call, varied repeat,
// answer a 3rd higher, final a tone lower before it comes to rest
static const int8_t k_center_off[4] = { 0, 0, 4, -2 };

typedef struct {
    int ch;          // channel the phrase plays on
    int layer_ch;    // -1, or a channel doubling it an octave below
    int max_notes;   // 0 = the whole phrase
    int octave;      // semitones added (e.g. -12)
    int vel_adj;
    int center_adj;
} play_opt_t;

// Play a 2-bar phrase of the motif from bar b. Targets are degrees of the
// chord under the note, steps run through the key's major scale (long steps
// that rub against the chord are nudged), approach notes lean into the note
// after them. The varied repeat changes its ending; the answer is the call
// with every target moved up a chord tone (or a bank template); the final
// phrase comes to rest on the root or 3rd and rings to the section end.
static void play_phrase(lofi_t *g, uint32_t b, uint32_t t0, const motif_t *m, int which, const play_opt_t *o)
{
    bool developed = which == PH_ANSWER && !m->answer_from_bank;
    const template_t *tp = (which == PH_ANSWER && m->answer_from_bank) ? &k_answers[m->answer] : &k_calls[m->call];
    int key = sect_of(g, b)->key;
    int base_vel = k_mel_sounds[m->sound].vel;

    tnote_t notes[12];
    int n = 0;
    for (int i = 0; i < tp->n; i++) {
        if (which == PH_VARIED && m->vary_cut && tp->n >= 3 && i == tp->n - 2) {
            continue;   // a breath before the changed ending
        }
        notes[n++] = tp->note[i];
    }
    tnote_t *end = &notes[n - 1];
    if (which == PH_VARIED) {
        end->val = (int8_t)m->vary_deg;
        end->dir = 0;
    } else if (developed) {
        end->val = (int8_t)m->answer_end;
        end->dir = 0;
    } else if (which == PH_FINAL) {
        end->val = (int8_t)m->final_deg;
        end->dir = 0;
        end->len = (uint8_t)(32 - end->start);
        end->vel = (int8_t)(end->vel - 8);
    }

    // Pitches: targets, steps and repeats in order; approach notes afterwards
    // from the note they lead into
    int pitch[12];
    int prev = clampi(m->center + k_center_off[which] + o->center_adj, MEL_LO, MEL_HI);
    for (int i = 0; i < n; i++) {
        const tnote_t *tn = &notes[i];
        if (tn->kind == K_A) {
            continue;
        }
        uint32_t bb = b + tn->start / 16;
        chord_t c = chord_at(g, bb, (tn->start % 16) * S16);
        int root = root_pc(g, bb, c);
        int p;
        switch (tn->kind) {
        case K_T: {
            int deg = tn->val, dir = tn->dir;
            if (developed && i != n - 1) {
                deg = k_up_tone[deg];
                if (tn->val == 7) {
                    dir = 1;   // the 9th above the 7th, not below
                }
            }
            p = target_pitch(root, c.q, deg, prev, dir);
            break;
        }
        case K_S:
            p = scale_step(key, prev, tn->val);
            if (tn->len >= 3 && avoid_tone(root, c.q, p)) {
                p = scale_step(key, p, tn->val > 0 ? 1 : -1);
            }
            break;
        default:   // K_R
            p = prev;
            break;
        }
        pitch[i] = p;
        prev = p;
    }
    for (int i = 0; i < n; i++) {
        if (notes[i].kind != K_A) {
            continue;
        }
        int next = i + 1 < n ? pitch[i + 1] : prev;
        pitch[i] = notes[i].val < 0 ? next - 1 : scale_step(key, next, 1);
        pitch[i] = clampi(pitch[i], MEL_LO - 1, MEL_HI);
    }

    int count = o->max_notes > 0 && o->max_notes < n ? o->max_notes : n;
    for (int i = 0; i < count; i++) {
        const tnote_t *tn = &notes[i];
        bool last = i == n - 1;
        uint32_t on = at(grid(t0, tn->start), MEL_LAG + jitter(g, HUM_KEYS));
        uint32_t off = grid(t0, tn->start + tn->len) - (last ? 2 * GAP : GAP);
        int vel = base_vel + tn->vel + o->vel_adj - ((tn->start & 1) ? 6 : 0);   // swung pickups are lighter
        vel = hum_v(g, vel, HUM_VEL_MEL);
        note(g, o->ch, on, off, pitch[i] + o->octave, vel);
        if (o->layer_ch >= 0) {
            note(g, o->layer_ch, on, off, pitch[i] + o->octave - 12, vel + LAYER_VEL_ADJ);
        }
    }
}

// Hook: call (bars 1-2), varied repeat (3-4), answer (5-6), call resolving
// (7-8). The final hook plays higher and doubled an octave below.
static void gen_hook_melody(lofi_t *g, uint32_t b, uint32_t t0, int sb, const sect_t *sc)
{
    static const int8_t k_which[4] = { PH_CALL, PH_VARIED, PH_ANSWER, PH_FINAL };
    if (sb % 2 != 0) {
        return;   // odd bars: the phrase started in the bar before covers them
    }
    play_opt_t o = {
        .ch = CH_MEL,
        .layer_ch = sc->final ? CH_LAYER : -1,
        .max_notes = 0,
        .octave = 0,
        .vel_adj = sc->dyn,
        .center_adj = sc->final ? 3 : 0,
    };
    play_phrase(g, b, t0, &sc->motif, k_which[sb / 2], &o);
}

// Groove: the first bar of the call, an octave down and soft, in bar 5 and
// (half of the grooves) bar 1, so the idea is in the ear before the hook
static void gen_groove_hint(lofi_t *g, uint32_t b, uint32_t t0, int sb, const sect_t *sc)
{
    if (sb != 4 && !(sb == 0 && chance(g, 50))) {
        return;
    }
    const template_t *tp = &k_calls[sc->motif.call];
    int first_bar = 0;
    while (first_bar < tp->n && tp->note[first_bar].start < 16) {
        first_bar++;
    }
    play_opt_t o = { .ch = CH_MEL, .layer_ch = -1, .max_notes = first_bar, .octave = -12,
                     .vel_adj = sc->dyn - 10, .center_adj = 0 };
    play_phrase(g, b, t0, &sc->motif, PH_CALL, &o);
}

// Break: the call's first three notes on the Rhodes top voice in bars 1 and 5
static void gen_break_echo(lofi_t *g, uint32_t b, uint32_t t0, int sb, const sect_t *sc)
{
    if (sb != 0 && sb != 4) {
        return;
    }
    play_opt_t o = { .ch = CH_RHODES, .layer_ch = -1, .max_notes = 3, .octave = -12,
                     .vel_adj = 72 - k_mel_sounds[sc->motif.sound].vel + sc->dyn, .center_adj = 0 };
    play_phrase(g, b, t0, &sc->motif, PH_CALL, &o);
}

// ---- Planning ----

static int pick_rhythm(lofi_t *g, lofi_section_t type, int keep)
{
    const int8_t *allowed = k_rhythm_allowed[type];
    if (keep >= 0 && in_list(allowed, keep) && chance(g, 60)) {
        return keep;
    }
    return pick_from(g, allowed, keep);
}

static int pick_hat(lofi_t *g, lofi_section_t type)
{
    if (type == LOFI_SEC_HOOK) {
        return chance(g, 70) ? 1 : 0;
    }
    if (type == LOFI_SEC_GROOVE) {
        return chance(g, 50) ? 0 : 2;
    }
    return 0;   // intro lead-in and the break's last bar
}

// Everything about a piece from its number: the generator is seeded from the
// number, the plan is drawn, and the state to continue the piece with is kept
static void plan_piece(lofi_t *g, uint32_t number, piece_t *p)
{
    uint32_t saved = g->rng;
    g->rng = piece_seed(number);
    for (int i = 0; i < 8; i++) {
        rnd_next(g);
    }

    p->number = number;
    p->bpm = rnd_range(g, 72, 86);
    p->key = k_keys[rnd(g, KEY_COUNT)];
    p->prog = (uint8_t)rnd(g, PROG_COUNT);
    p->break_prog = (uint8_t)other(g, p->prog, PROG_COUNT);
    int r = rnd(g, 10);
    p->lift = (uint8_t)(r < 4 ? 1 : (r < 8 ? 2 : 5));   // semitone, tone, or up a 4th
    make_motif(g, &p->motif);
    p->rhythm = (uint8_t)pick_rhythm(g, LOFI_SEC_INTRO, -1);
    p->kick = (uint8_t)rnd(g, KICK_COUNT);
    p->hat = 0;
    p->bass = (uint8_t)rnd(g, BASS_FIG_COUNT);

    p->rng_after = g->rng;
    g->rng = saved;
}

// Section `idx` of piece p (the piece starts at global bar piece_bar)
static void fill_sect(sect_t *s, const piece_t *p, int idx, uint32_t piece_bar)
{
    const form_t *f = &k_form[idx];
    memset(s, 0, sizeof(*s));
    s->type = (lofi_section_t)f->type;
    s->form_idx = (uint8_t)idx;
    s->bars = f->bars;
    s->dyn = f->dyn;
    s->final = f->final;
    s->piece = p->number;
    s->piece_bar = piece_bar;
    s->bpm = p->bpm;
    s->key = (uint8_t)((p->key + (idx >= FINAL_IDX ? p->lift : 0)) % 12);
    s->prog = s->type == LOFI_SEC_BREAK ? p->break_prog : p->prog;
    s->motif = p->motif;
    s->rhythm = p->rhythm;
    s->kick = p->kick;
    s->hat = p->hat;
    s->bass = p->bass;
}

static void plan_next(lofi_t *g)
{
    const sect_t *cu = &g->cur;
    sect_t *nx = &g->nxt;
    int idx = cu->form_idx + 1;

    if (idx >= FORM_LEN) {
        // The next piece: planned from its number only, its intro uses the
        // patterns the plan chose
        plan_piece(g, next_piece_number(cu->piece), &g->next_piece);
        fill_sect(nx, &g->next_piece, 0, cu->piece_bar + PIECE_BARS);
        return;
    }

    fill_sect(nx, &g->piece, idx, cu->piece_bar);
    bool same = nx->type == cu->type;
    nx->rhythm = (uint8_t)pick_rhythm(g, nx->type, same ? cu->rhythm : -1);
    nx->hat = (uint8_t)(nx->final ? 1 : pick_hat(g, nx->type));
    nx->kick = cu->kick;
    nx->bass = cu->bass;
    if (!same && chance(g, 40)) {
        nx->kick = (uint8_t)other(g, cu->kick, KICK_COUNT);
    }
    if (!same && chance(g, 40)) {
        nx->bass = (uint8_t)other(g, cu->bass, BASS_FIG_COUNT);
    }
}

// ---- Parts ----

static void setup_channels(lofi_t *g)
{
    static const struct {
        uint8_t ch, prog, vol, pan, expr, rev;
    } k_setup[] = {
        { CH_RHODES, PROG_RHODES, 100, 58, 112, 56 },
        { CH_BASS, PROG_BASS, 110, 64, 127, 10 },
        { CH_PAD, PROG_PAD, 56, 84, 100, 90 },
        { CH_MEL, 5, 92, 44, 127, 60 },
        { CH_LAYER, 11, LAYER_VOL, 70, 127, 60 },
        { CH_DRUMS, 0, 104, 64, 127, 28 },
    };
    for (size_t i = 0; i < sizeof(k_setup) / sizeof(k_setup[0]); i++) {
        int ch = k_setup[i].ch;
        cc(g, ch, 0, 101, 0);     // RPN 0: pitch bend range
        cc(g, ch, 0, 100, 0);
        cc(g, ch, 0, 6, 2);       // 2 semitones
        cc(g, ch, 0, 38, 0);
        cc(g, ch, 0, 101, 127);   // RPN null
        cc(g, ch, 0, 100, 127);
        cc(g, ch, 0, 7, k_setup[i].vol);
        cc(g, ch, 0, 10, k_setup[i].pan);
        cc(g, ch, 0, 11, k_setup[i].expr);
        cc(g, ch, 0, 91, k_setup[i].rev);
        put(g, 0, (uint8_t)(0xC0 | ch), k_setup[i].prog, 0);
    }
    put(g, 0, 0xE0 | CH_RHODES, BEND_CENTER & 0x7F, BEND_CENTER >> 7);
    g->rhodes_expr = 112;
    g->pad_expr = 100;
}

// At a piece's first bar: its tempo, its lead sound and the doubling sound
static void start_piece(lofi_t *g, uint32_t t0)
{
    const motif_t *m = &g->cur.motif;
    put_tempo(g, at(t0, -60), g->cur.bpm, 100);
    put(g, at(t0, -60), 0xC0 | CH_MEL, k_mel_sounds[m->sound].prog, 0);
    cc(g, CH_MEL, at(t0, -60), 7, k_mel_sounds[m->sound].vol);
    put(g, at(t0, -60), 0xC0 | CH_LAYER, k_mel_sounds[m->sound].layer, 0);
}

typedef struct {
    int s16;    // start on this bar's 16th grid
    int e16;    // end on the grid, may run into the next bar (> 16)
    int root;   // chord root pitch class
    int q;
    int vel;
} hit_t;

static void add_hit(hit_t *h, int *n, int s16, int e16, int vel, int root, chord_t c)
{
    h[*n] = (hit_t){ .s16 = s16, .e16 = e16, .root = root, .q = c.q, .vel = vel };
    (*n)++;
}

// Rolled bottom-up, a little behind the beat
static void play_chord(lofi_t *g, uint32_t t0, const hit_t *h, int vel_adj, int roll_ms, uint32_t end_override)
{
    uint8_t v[4];
    voice_rhodes(g, h->root, h->q, v);
    uint32_t start = at(grid(t0, h->s16), RHODES_LAG + jitter(g, HUM_KEYS));
    uint32_t end = end_override ? end_override : grid(t0, h->e16) - GAP;
    int roll = ms_ticks(g, roll_ms);
    for (int i = 0; i < 4; i++) {
        int vel = h->vel + vel_adj + (i == 3 ? 4 : (i == 0 ? -2 : 0));
        note(g, CH_RHODES, start + (uint32_t)(i * roll), end, v[i], hum_v(g, vel, HUM_VEL));
    }
}

// Where a chord pushed into bar b on the previous 4& has to end: at that
// bar's first real hit, its beat 3 if the chord changes there, or its own push
static int push_end(const lofi_t *g, uint32_t b)
{
    const rpat_t *pat = &k_rhodes_pats[sect_of(g, b)->rhythm];
    int end = 32;
    for (int i = 0; i < pat->n; i++) {
        if (pat->hit[i].s16 > 0 && 16 + pat->hit[i].s16 < end) {
            end = 16 + pat->hit[i].s16;
        }
    }
    if (bar_chords(g, b)->n == 2 && end > 24) {
        end = 24;
    }
    if (pat->push && end > 16 + PUSH_S16) {
        end = 16 + PUSH_S16;
    }
    return end;
}

static void gen_rhodes(lofi_t *g, uint32_t b, uint32_t t0, int vel_adj)
{
    const rpat_t *pat = &k_rhodes_pats[sect_of(g, b)->rhythm];
    const bar_t *bc = bar_chords(g, b);
    hit_t h[6];
    int n = 0;
    bool hit_on_3 = false;

    for (int i = 0; i < pat->n; i++) {
        const rhit_t *r = &pat->hit[i];
        if (r->s16 == 0 && g->anticipated) {
            continue;   // already sounding from the push at the end of the last bar
        }
        int end = r->e16;
        if (bc->n == 2 && r->s16 < 8 && end > 8) {
            end = 8;    // the chord changes on beat 3
        }
        if (r->s16 == 8) {
            hit_on_3 = true;
        }
        chord_t c = chord_at(g, b, r->s16 * S16);
        add_hit(h, &n, r->s16, end, r->vel, root_pc(g, b, c), c);
    }
    if (bc->n == 2 && !hit_on_3) {
        int end = pat->push ? PUSH_S16 : 16;
        add_hit(h, &n, 8, end, 64, root_pc(g, b, bc->c[1]), bc->c[1]);
    }

    // Push: the next bar's first chord on this 4&, tied over
    g->anticipated = pat->push;
    if (pat->push) {
        chord_t c = chord_at(g, b + 1, 0);
        add_hit(h, &n, PUSH_S16, push_end(g, b + 1), 66, root_pc(g, b + 1, c), c);
    }

    // Sort by start, then each hit ends where the next one starts
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && h[j].s16 < h[j - 1].s16; j--) {
            hit_t t = h[j];
            h[j] = h[j - 1];
            h[j - 1] = t;
        }
    }
    for (int i = 0; i + 1 < n; i++) {
        if (h[i].e16 > h[i + 1].s16) {
            h[i].e16 = h[i + 1].s16;
        }
    }
    for (int i = 0; i < n; i++) {
        play_chord(g, t0, &h[i], vel_adj, rnd_range(g, ROLL_MS_MIN, ROLL_MS_MAX), 0);
    }
}

static int bass_root(int pc)
{
    return BASS_LO + (pc - BASS_LO % 12 + 12) % 12;
}

// Approach into the next root: a chromatic neighbour or its fifth
static int bass_approach(lofi_t *g, int rn)
{
    int ap;
    if (chance(g, 60)) {
        ap = rn + (chance(g, 50) ? 1 : -1);
        if (ap < BASS_LO) {
            ap = rn + 1;
        }
    } else {
        ap = rn + 7;
        if (ap > BASS_HI) {
            ap -= 12;
        }
    }
    return ap;
}

// Locked to the kick: the root on every kick of the bar (the section's figure
// may put a fifth or octave on the 2nd and 3rd), legato up to two beats, and
// an approach note on 4a into the next bar's chord. In `held` mode (break)
// the root just holds the bar.
static void gen_bass(lofi_t *g, uint32_t b, uint32_t t0, bool held, int vel_adj)
{
    const sect_t *sc = sect_of(g, b);
    const bar_t *bc = bar_chords(g, b);
    int starts[6], pitches[6], vels[6], n = 0;

    if (held) {
        starts[n] = 0;
        pitches[n] = bass_root(root_pc(g, b, bc->c[0]));
        vels[n++] = 92;
        if (bc->n == 2) {
            starts[n] = 8;
            pitches[n] = bass_root(root_pc(g, b, bc->c[1]));
            vels[n++] = 84;
        }
    } else {
        const int8_t *kk = k_kicks[sc->kick].k[b % 2];
        for (int i = 0; i < 4 && kk[i] >= 0; i++) {
            chord_t c = chord_at(g, b, kk[i] * S16);
            int r = bass_root(root_pc(g, b, c));
            int iv = i < 3 ? k_bass_figs[sc->bass][i] : 0;
            int p = r + iv;
            if (p > BASS_HI) {
                p = iv == 12 ? r : r - 5;   // fifth below instead
            }
            starts[n] = kk[i];
            pitches[n] = p;
            vels[n++] = i == 0 ? 100 : (iv == 12 ? 80 : 90);
        }
    }

    chord_t cn = chord_at(g, b + 1, 0);
    int rn = bass_root(root_pc(g, b + 1, cn));
    int last_root = bass_root(root_pc(g, b, chord_at(g, b, 15 * S16)));
    int ap_s16 = held ? 14 : 15;
    if (rn != last_root && starts[n - 1] < ap_s16 && chance(g, held ? 40 : 50)) {
        starts[n] = ap_s16;
        pitches[n] = bass_approach(g, rn);
        vels[n++] = 78;
    }

    for (int i = 0; i < n; i++) {
        uint32_t on = grid(t0, starts[i]);
        uint32_t off = i + 1 < n ? grid(t0, starts[i + 1]) - GAP : t0 + BAR - GAP;
        if (!held && starts[i] < 14 && off > on + 2 * BEAT) {
            off = on + 2 * BEAT;
        }
        note(g, CH_BASS, on, off, pitches[i], hum_v(g, vels[i] + vel_adj, HUM_VEL));
    }
}

static void drum(lofi_t *g, int key, uint32_t t, int vel, int len)
{
    note(g, CH_DRUMS, t, t + (uint32_t)len, key, vel);
}

static void snare(lofi_t *g, uint32_t t0, int s16, int vel)
{
    drum(g, DR_SNARE, at(grid(t0, s16), SNARE_LAG), hum_v(g, vel, HUM_VEL_DRUM), DRUM_LEN);
}

static void kick(lofi_t *g, uint32_t t0, int s16, int vel)
{
    drum(g, DR_KICK, grid(t0, s16), hum_v(g, vel, HUM_VEL_DRUM), DRUM_LEN);
}

static void hat(lofi_t *g, uint32_t t0, int s16, int vel, bool open)
{
    drum(g, open ? DR_OPEN_HAT : DR_HAT, grid(t0, s16), hum_v(g, vel, HUM_VEL_HAT), open ? BEAT / 2 : DRUM_LEN);
}

static void hats(lofi_t *g, uint32_t t0, int pattern, int limit, int open_s16, int vel_adj)
{
    const uint8_t *hv = k_hats[pattern];
    for (int s = 0; s < limit; s++) {
        if (hv[s]) {
            hat(g, t0, s, hv[s] + vel_adj, s == open_s16);
        }
    }
}

typedef enum { FILL_NONE, FILL_ROLL, FILL_KICK, FILL_BREATH } fill_t;

// Full kit for one bar. The fill replaces beat 4 in a section's last bar:
// ROLL = snare 4, 4&, 4a rising; KICK = snare 4, kick 4e, snare 4&;
// BREATH = the band drops out after beat 2, snare on 4, kick pickup on 4a.
static void gen_drums(lofi_t *g, uint32_t b, uint32_t t0, int sb, fill_t fill, int vel_adj)
{
    const sect_t *sc = sect_of(g, b);
    const kick_t *kp = &k_kicks[sc->kick];
    const int8_t *kk = kp->k[b % 2];

    int kick_limit = fill == FILL_BREATH ? 8 : (fill == FILL_KICK ? 12 : 16);
    for (int i = 0; i < 4 && kk[i] >= 0; i++) {
        if (kk[i] < kick_limit) {
            kick(g, t0, kk[i], (kk[i] == 0 ? 112 : 100) + vel_adj);
        }
    }

    snare(g, t0, 4, 102 + vel_adj);
    switch (fill) {
    case FILL_ROLL:
        snare(g, t0, 12, 102 + vel_adj);
        snare(g, t0, 14, 62 + vel_adj);
        snare(g, t0, 15, 84 + vel_adj);
        break;
    case FILL_KICK:
        snare(g, t0, 12, 102 + vel_adj);
        kick(g, t0, 13, 92 + vel_adj);
        snare(g, t0, 14, 72 + vel_adj);
        break;
    case FILL_BREATH:
        snare(g, t0, 12, 106 + vel_adj);
        kick(g, t0, 15, 92 + vel_adj);
        break;
    default:
        snare(g, t0, 12, 102 + vel_adj);
        if (sb % 2 == 1 && kp->ghost >= 0) {
            snare(g, t0, kp->ghost, rnd_range(g, 40, 46));
        }
        break;
    }

    int hat_limit = fill == FILL_BREATH ? 8 : (fill == FILL_NONE ? 16 : 13);
    hats(g, t0, sc->hat, hat_limit, (fill == FILL_NONE && sb % 4 == 3) ? 14 : -1, vel_adj);
}

// Break: side stick on 2 and 4; soft quarter hats from the 5th bar on
static void gen_break_drums(lofi_t *g, uint32_t t0, int sb, int vel_adj)
{
    drum(g, DR_STICK, at(grid(t0, 4), SNARE_LAG), hum_v(g, 80 + vel_adj, HUM_VEL_DRUM), DRUM_LEN);
    drum(g, DR_STICK, at(grid(t0, 12), SNARE_LAG), hum_v(g, 80 + vel_adj, HUM_VEL_DRUM), DRUM_LEN);
    if (sb >= 4) {
        hats(g, t0, HAT_BREAK, 16, -1, vel_adj);
    }
}

// Intro's last bar: hats walk in from beat 3, snare on 4, kick pickup on 4a
static void gen_leadin(lofi_t *g, uint32_t t0, int vel_adj)
{
    static const uint8_t k_vel[4] = { 46, 52, 58, 64 };
    for (int i = 0; i < 4; i++) {
        hat(g, t0, 8 + 2 * i, k_vel[i] + vel_adj, false);
    }
    snare(g, t0, 12, 96 + vel_adj);
    kick(g, t0, 15, 96 + vel_adj);
}

// 3rd, 5th, 7th: the chord without its root
static void upper3(int root, int q, int pcs[3])
{
    for (int i = 0; i < 3; i++) {
        pcs[i] = (root + k_tones[q][i + 1]) % 12;
    }
}

static uint16_t pad_mask(int root, int q)
{
    uint16_t m = tone_mask(root, q);
    for (int i = 0; i < 4; i++) {
        m |= (uint16_t)(1u << ((root + k_voicings[q].iv[0][i]) % 12));
    }
    return m;
}

static void pad_release(lofi_t *g, uint32_t tick)
{
    if (g->pad_on) {
        for (int i = 0; i < 3; i++) {
            put(g, tick, 0x80 | CH_PAD, g->pad_held[i], 0);
        }
        g->pad_on = false;
    }
}

// Move the pad to this voicing at `tick`; a voicing that is already sounding
// just keeps ringing (no re-attack every bar)
static void pad_set(lofi_t *g, const int pcs[3], uint32_t tick, int vel_adj)
{
    uint8_t v[3];
    memcpy(v, g->pad_prev, 3);
    place(g, pcs, 3, PAD_LO, PAD_HI, g->pad_prev, v);
    memcpy(g->pad_prev, v, 3);
    if (g->pad_on && memcmp(v, g->pad_held, 3) == 0) {
        return;
    }
    pad_release(g, tick - GAP);
    for (int i = 0; i < 3; i++) {
        put(g, tick, 0x90 | CH_PAD, v[i], (uint8_t)hum_v(g, 58 + vel_adj, 3));
    }
    memcpy(g->pad_held, v, 3);
    g->pad_on = true;
}

static void gen_pad(lofi_t *g, uint32_t b, uint32_t t0, int vel_adj)
{
    const bar_t *bc = bar_chords(g, b);
    int pcs[3];
    int r0 = root_pc(g, b, bc->c[0]);
    if (bc->n == 1) {
        upper3(r0, bc->c[0].q, pcs);
        pad_set(g, pcs, t0, vel_adj);
        return;
    }

    // Two chords: hold tones common to both, roots dropped first
    int r1 = root_pc(g, b, bc->c[1]);
    uint16_t common = pad_mask(r0, bc->c[0].q) & pad_mask(r1, bc->c[1].q);
    if (__builtin_popcount(common) > 3) {
        common &= (uint16_t)~(1u << r1);
    }
    if (__builtin_popcount(common) > 3) {
        common &= (uint16_t)~(1u << r0);
    }
    if (__builtin_popcount(common) >= 3) {
        int list[12], n = 0;
        for (int pc = 0; pc < 12; pc++) {
            if ((common >> pc) & 1) {
                list[n++] = pc;
            }
        }
        while (n > 3) {   // drop random extras
            int k = rnd(g, n);
            list[k] = list[--n];
        }
        pad_set(g, list, t0, vel_adj);
        return;
    }
    // Too little in common: change at mid-bar
    upper3(r0, bc->c[0].q, pcs);
    pad_set(g, pcs, t0, vel_adj);
    upper3(r1, bc->c[1].q, pcs);
    pad_set(g, pcs, t0 + BAR / 2, vel_adj);
}

static fill_t fill_before(lofi_t *g, lofi_section_t next)
{
    switch (next) {
    case LOFI_SEC_HOOK:
        return FILL_BREATH;
    case LOFI_SEC_BREAK:
        return FILL_KICK;
    case LOFI_SEC_OUTRO:
        return FILL_ROLL;
    default:
        return chance(g, 50) ? FILL_ROLL : FILL_KICK;
    }
}

// Outro, 4 bars on Imaj9 with a ritardando, then one silent bar:
// bar 1 chord, pad, bass root, kick on 1, the lead's resting note (2 bars);
// bar 2 chord again, softer; bar 3 the final slow roll with the bass and the
// resting note again, ringing through bar 4 into the gap; bar 5 silence and
// the next piece's tempo.
static void gen_outro(lofi_t *g, uint32_t b, uint32_t t0, int sb)
{
    const sect_t *sc = &g->cur;
    int dyn = sc->dyn;
    const motif_t *m = &sc->motif;
    chord_t c = { 0, Q_MAJ9 };
    int root = sc->key;

    if (sb >= OUTRO_BARS) {
        put_tempo(g, t0, g->nxt.bpm, 100);   // the gap runs at the next piece's tempo
        return;
    }

    // Ritardando: a tempo step every half beat across the 4 bars
    for (int h = 0; h < 8; h++) {
        int step = sb * 8 + h;
        put_tempo(g, t0 + (uint32_t)h * BEAT / 2, sc->bpm, 100 - (100 - RIT_END_PCT) * step / (OUTRO_BARS * 8 - 1));
    }

    int rest = target_pitch(root, Q_MAJ9, m->final_deg, m->center + k_center_off[PH_FINAL], 0);
    int mel_vel = k_mel_sounds[m->sound].vel + dyn;
    hit_t h = { .s16 = 0, .e16 = 16, .root = root, .q = Q_MAJ9, .vel = 66 };
    int pcs[3];

    switch (sb) {
    case 0:
        play_chord(g, t0, &h, dyn, rnd_range(g, ROLL_MS_MIN, ROLL_MS_MAX), 0);
        upper3(root, c.q, pcs);
        pad_set(g, pcs, t0, dyn / 2);
        note(g, CH_BASS, t0, t0 + 2 * BAR - GAP, bass_root(root), hum_v(g, 92 + dyn / 2, HUM_VEL));
        kick(g, t0, 0, 104 + dyn / 2);
        note(g, CH_MEL, at(t0, MEL_LAG), t0 + 2 * BAR - 2 * GAP, rest, hum_v(g, mel_vel, HUM_VEL_MEL));
        break;
    case 1:
        h.vel = 60;
        play_chord(g, t0, &h, dyn, rnd_range(g, ROLL_MS_MIN, ROLL_MS_MAX), 0);
        break;
    case 2: {
        uint32_t ring = t0 + 2 * BAR + BEAT;   // one beat into the gap
        play_chord(g, t0, &h, dyn + 4, ROLL_FINAL_MS, ring);
        note(g, CH_BASS, t0, ring - BEAT / 2, bass_root(root), hum_v(g, 94 + dyn / 2, HUM_VEL));
        note(g, CH_MEL, at(t0, MEL_LAG + ms_ticks(g, ROLL_FINAL_MS) * 4), t0 + 2 * BAR - GAP, rest,
             hum_v(g, mel_vel - 6, HUM_VEL_MEL));
        pad_release(g, ring);
        break;
    }
    default:
        break;   // bar 4: everything rings, the tempo keeps falling
    }
}

static void gen_bar(lofi_t *g)
{
    uint32_t b = g->bar;
    if (b == g->section_bar + g->cur.bars) {
        g->section++;
        g->section_bar = b;
        g->cur = g->nxt;
        if (g->cur.form_idx == 0) {
            // A new piece: continue with its own generator state and start
            // its voice leading from the fixed reference
            static const uint8_t k_rhodes_start[4] = { 55, 60, 64, 67 };   // G3 C4 E4 G4
            static const uint8_t k_pad_start[3] = { 60, 64, 67 };
            g->piece = g->next_piece;
            g->rng = g->piece.rng_after;
            memcpy(g->rhodes_prev, k_rhodes_start, sizeof(k_rhodes_start));
            memcpy(g->pad_prev, k_pad_start, sizeof(k_pad_start));
        }
        plan_next(g);
    }

    uint32_t t0 = g->bar_tick;
    const sect_t *sc = &g->cur;
    int sb = (int)(b - g->section_bar);
    bool first = sb == 0;
    bool last = sb == sc->bars - 1;
    int dyn = sc->dyn;

    if (first && sc->form_idx == 0) {
        start_piece(g, t0);
    }

    // Expression arc: drops happen in a section's first bar, rises in the
    // last bar of the section before (4 CC11 steps each)
    if (first) {
        int r = rhodes_expr(sc), p = pad_expr(sc);
        if (r < g->rhodes_expr) {
            ramp(g, CH_RHODES, t0, g->rhodes_expr, r);
            g->rhodes_expr = r;
        }
        if (p < g->pad_expr) {
            ramp(g, CH_PAD, t0, g->pad_expr, p);
            g->pad_expr = p;
        }
    }
    if (last) {
        int r = rhodes_expr(&g->nxt), p = pad_expr(&g->nxt);
        if (r > g->rhodes_expr) {
            ramp(g, CH_RHODES, t0, g->rhodes_expr, r);
            g->rhodes_expr = r;
        }
        if (p > g->pad_expr) {
            ramp(g, CH_PAD, t0, g->pad_expr, p);
            g->pad_expr = p;
        }
    }

    switch (sc->type) {
    case LOFI_SEC_INTRO:   // Rhodes alone, bass from bar 5, hats walk in on bar 8
        pad_release(g, t0 - GAP);
        gen_rhodes(g, b, t0, dyn);
        if (sb >= 4) {
            gen_bass(g, b, t0, false, dyn / 2);
        }
        if (last) {
            gen_leadin(g, t0, dyn / 2);
        }
        break;
    case LOFI_SEC_GROOVE:
        pad_release(g, t0 - GAP);
        gen_rhodes(g, b, t0, dyn);
        gen_bass(g, b, t0, false, dyn / 2);
        gen_drums(g, b, t0, sb, last ? fill_before(g, g->nxt.type) : FILL_NONE, dyn / 2);
        gen_groove_hint(g, b, t0, sb, sc);
        break;
    case LOFI_SEC_HOOK:
        gen_rhodes(g, b, t0, dyn);
        gen_bass(g, b, t0, false, dyn / 2);
        gen_drums(g, b, t0, sb, last ? fill_before(g, g->nxt.type) : FILL_NONE, dyn / 2);
        gen_pad(g, b, t0, dyn / 2);
        gen_hook_melody(g, b, t0, sb, sc);
        break;
    case LOFI_SEC_BREAK:   // pad and Rhodes hold, bass holds roots, side stick; the kit returns in bar 8
        gen_rhodes(g, b, t0, dyn);
        gen_bass(g, b, t0, !last, dyn / 2);
        gen_pad(g, b, t0, dyn / 2);
        gen_break_echo(g, b, t0, sb, sc);
        if (last) {
            gen_drums(g, b, t0, sb, FILL_ROLL, dyn / 2);
        } else {
            gen_break_drums(g, t0, sb, dyn / 2);
        }
        break;
    default:
        gen_outro(g, b, t0, sb);
        break;
    }

    g->bar++;
    g->bar_tick += BAR;
}

// ---- Output filter ----

static int ev_rank(uint8_t status)
{
    uint8_t type = status & 0xF0;
    return type == 0x80 ? 0 : (type == 0x90 ? 2 : 1);   // offs, then controls and tempo, then ons
}

static int cmp_pend(const void *a, const void *b)
{
    const pend_t *x = a;
    const pend_t *y = b;
    if (x->ev.tick != y->ev.tick) {
        return x->ev.tick < y->ev.tick ? -1 : 1;
    }
    int rx = ev_rank(x->ev.status), ry = ev_rank(y->ev.status);
    if (rx != ry) {
        return rx - ry;
    }
    return x->seq < y->seq ? -1 : (x->seq > y->seq ? 1 : 0);
}

static void emit_off(mev_t *out, int *n, uint32_t tick, int ch, int key)
{
    out[(*n)++] = (mev_t){ .tick = tick, .status = (uint8_t)(0x80 | ch), .d1 = (uint8_t)key };
}

// Budget full: end the oldest Rhodes note early (not one from the chord being rolled)
static bool steal_rhodes(lofi_t *g, uint32_t tick, mev_t *out, int *n)
{
    int best = -1;
    for (int k = 0; k < 128; k++) {
        if (g->sounding[CH_RHODES][k] && tick - g->rhodes_on[k] >= STEAL_MIN_AGE &&
            (best < 0 || g->rhodes_on[k] < g->rhodes_on[best])) {
            best = k;
        }
    }
    if (best < 0) {
        return false;
    }
    emit_off(out, n, tick, CH_RHODES, best);
    g->sounding[CH_RHODES][best] = 0;
    g->active--;
    g->shortened++;
    return true;
}

// Writes at most 2 events
static void emit(lofi_t *g, const mev_t *e, mev_t *out, int *n)
{
    uint8_t type = e->status & 0xF0;
    int ch = e->status & 0x0F;
    int key = e->d1 & 0x7F;

    if (e->status == MEV_TEMPO) {
        out[(*n)++] = *e;
    } else if (type == 0x90) {
        g->depth[ch][key]++;
        if (g->sounding[ch][key]) {
            emit_off(out, n, e->tick, ch, key);   // same pitch again: end the old note first
        } else {
            if (g->active >= POLY_BUDGET && !steal_rhodes(g, e->tick, out, n)) {
                g->dropped++;
                return;
            }
            g->sounding[ch][key] = 1;
            if (++g->active > g->peak) {
                g->peak = g->active;
            }
        }
        if (ch == CH_RHODES) {
            g->rhodes_on[key] = e->tick;
        }
        out[(*n)++] = *e;
    } else if (type == 0x80) {
        if (g->depth[ch][key] == 0) {
            return;
        }
        // Only the last outstanding note of this pitch ends it
        if (--g->depth[ch][key] == 0 && g->sounding[ch][key]) {
            g->sounding[ch][key] = 0;
            g->active--;
            out[(*n)++] = *e;
        }
    } else {
        out[(*n)++] = *e;
    }
}

// ---- Public API ----

lofi_t *lofi_create(uint32_t first_piece)
{
    lofi_t *g = calloc(1, sizeof(*g));
    if (!g) {
        return NULL;
    }
    g->rng = 0x9E3779B9u;
    plan_piece(g, first_piece % LOFI_PIECES, &g->piece);
    g->rng = g->piece.rng_after;
    fill_sect(&g->cur, &g->piece, 0, 0);
    plan_next(g);

    static const uint8_t k_rhodes_start[4] = { 55, 60, 64, 67 };   // G3 C4 E4 G4
    static const uint8_t k_pad_start[3] = { 60, 64, 67 };
    memcpy(g->rhodes_prev, k_rhodes_start, sizeof(k_rhodes_start));
    memcpy(g->pad_prev, k_pad_start, sizeof(k_pad_start));

    g->bar_tick = START_TICK;
    setup_channels(g);
    put_tempo(g, 0, g->cur.bpm, 100);
    return g;
}

void lofi_destroy(lofi_t *g)
{
    free(g);
}

int lofi_bpm(const lofi_t *g)
{
    return g->cur.bpm;
}

int lofi_pull(lofi_t *g, uint32_t horizon, mev_t *out, int max)
{
    while (g->bar_tick <= horizon + GEN_MARGIN) {
        gen_bar(g);
    }
    if (g->dirty) {
        qsort(g->pend, (size_t)g->npend, sizeof(pend_t), cmp_pend);
        g->dirty = false;
    }

    int n = 0, i = 0;
    while (i < g->npend && g->pend[i].ev.tick <= horizon && n + 2 <= max) {
        emit(g, &g->pend[i].ev, out, &n);
        i++;
    }
    if (i > 0) {
        memmove(g->pend, g->pend + i, (size_t)(g->npend - i) * sizeof(pend_t));
        g->npend -= i;
    }
    return n;
}

int lofi_stop_msgs(midi_msg_t *out, int max)
{
    static const uint8_t k_chans[] = { CH_RHODES, CH_BASS, CH_PAD, CH_MEL, CH_LAYER, CH_DRUMS };
    int n = 0;
    if (n < max) {
        out[n++] = (midi_msg_t){ .len = 3, .b = { 0xE0 | CH_RHODES, BEND_CENTER & 0x7F, BEND_CENTER >> 7 } };
    }
    for (size_t i = 0; i < sizeof(k_chans) && n < max; i++) {
        out[n++] = (midi_msg_t){ .len = 3, .b = { (uint8_t)(0xB0 | k_chans[i]), 11, 127 } };
    }
    return n;
}

void lofi_status(const lofi_t *g, lofi_status_t *st)
{
    st->piece = g->cur.piece;
    st->section = g->section;
    st->bar = g->section_bar;
    st->type = g->cur.type;
    st->key = g->cur.key;
    st->progression = g->piece.prog;
    st->break_progression = g->piece.break_prog;
    st->key_lift = g->piece.lift;
    st->rhodes_rhythm = g->cur.rhythm;
    st->kick_pattern = g->cur.kick;
    st->hat_pattern = g->cur.hat;
    st->bass_figure = g->cur.bass;
    st->motif = g->cur.motif.call;
    st->sound = g->cur.motif.sound;
    st->bpm = g->cur.bpm;
    st->peak_notes = g->peak;
    st->shortened = g->shortened;
    st->dropped = g->dropped;
    st->overflow = g->overflow;
}

const char *lofi_section_name(lofi_section_t type)
{
    static const char *const k_names[] = { "intro", "groove", "hook", "break", "outro" };
    return (unsigned)type < 5 ? k_names[type] : "?";
}

const char *lofi_key_name(int pitch_class)
{
    static const char *const k_names[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return k_names[((pitch_class % 12) + 12) % 12];
}
