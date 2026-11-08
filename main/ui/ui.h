#pragma once

// Screen UI for the FM-1 Song Box: BLE status, song list, play/stop,
// tempo and transpose. Pure view: it holds no player or BLE logic.
//
// Threading
// - ui_init() and every ui_set_*() take the LVGL lock internally, so they can
//   be called from any task (BLE, player, web server).
// - Callbacks run in the LVGL task while it holds the LVGL lock. They may call
//   ui_set_*() directly (the lock is recursive). Do not block in them.
// - Do not call ui_set_*() while holding a lock that a callback also takes,
//   or the two tasks can deadlock.
//
// Ownership: the app owns tempo, transpose, playing state and BLE state.
// The UI only reports intents through callbacks; the app applies and clamps
// the value and then calls the matching setter so the screen shows it.

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ranges shared with the app so both sides clamp the same way.
// The UI greys out a stepper's - or + at the end of its range.
#define UI_TEMPO_MIN        50
#define UI_TEMPO_MAX        150
#define UI_TEMPO_STEP       5
#define UI_TEMPO_DEFAULT    100
#define UI_TRANSPOSE_MIN    (-12)
#define UI_TRANSPOSE_MAX    12
#define UI_TRANSPOSE_DEFAULT 0

typedef enum {
    UI_BLE_SCANNING = 0,
    UI_BLE_CONNECTING,
    UI_BLE_CONNECTED,
    UI_BLE_DISCONNECTED,
} ui_ble_state_t;

typedef struct {
    void (*on_play)(int index);           // Play pressed with song `index` selected
    void (*on_stop)(void);                // Stop pressed while playing
    void (*on_tempo_step)(int dir);       // -1 or +1 (one UI_TEMPO_STEP)
    void (*on_transpose_step)(int dir);   // -1 or +1 semitone
    void (*on_play_hold)(int index);      // Play held down while stopped (lo-fi: replay the last piece)
} ui_callbacks_t;

// Build the screen on `disp`. `cb` is copied; any member may be NULL.
void ui_init(lv_display_t *disp, const ui_callbacks_t *cb);

// Top bar status. `name` is the device name shown when connected (e.g. "FM-1");
// ignored for the other states. NULL is fine.
void ui_set_ble_status(ui_ble_state_t state, const char *name);

// Replace the song list. Names are copied. Keeps the selection on the same
// name if it is still there, otherwise selects the first song.
// Clears the playing mark and all unsupported marks: re-apply them after.
void ui_set_songs(const char *const *names, int count);

// Same, with the first `pinned` names shown as special entries (loop icon
// instead of a track number; numbering starts after them). Used for the
// "Lo-fi Generator" row.
void ui_set_songs_pinned(const char *const *names, int count, int pinned);

// Mark the song being played (-1 = none). Play turns into Stop while >= 0.
void ui_set_playing(int index);

// Change the title shown on a row (e.g. "Lo-fi Generator #4821"). Kept until
// the next ui_set_songs*(). A ".mid" ending is hidden as for file names.
void ui_set_song_title(int index, const char *title);

// Mark a song that failed to parse. Its row shows "Unsupported file" and
// Play stays disabled while it is selected.
void ui_set_song_unsupported(int index);

void ui_set_tempo(int percent);
void ui_set_transpose(int semitones);

#ifdef __cplusplus
}
#endif
