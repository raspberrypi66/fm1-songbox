#include "app.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "ble_midi.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lofi_source.h"
#include "player.h"
#include "smf.h"
#include "smf_source.h"
#include "songs.h"
#include "ui.h"

static const char *TAG = "app";

#define LOFI_ROW 0   // pinned first entry; songs follow from row 1
#define LOFI_TITLE "Lo-fi Generator"
#define LOFI_PIECES 10000   // piece numbers 0..9999; same number, same piece
#define PATH_MAX_LEN 300

static char **s_songs;          // file names in /songs, song i is row i + 1
static int s_nsongs;
static const char **s_rows;     // "Lo-fi Generator" + s_songs

static atomic_bool s_connected;
static atomic_int s_playing = -1;

// Lo-fi piece shown on its row and replayed by holding Play
static atomic_uint s_piece;
static atomic_bool s_piece_valid;
static esp_timer_handle_t s_title_timer;
static int s_tempo = UI_TEMPO_DEFAULT;          // LVGL task only
static int s_transpose = UI_TRANSPOSE_DEFAULT;  // LVGL task only

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void song_path(int row, char *path, size_t len)
{
    snprintf(path, len, SONGS_DIR "/%s", s_songs[row - 1]);
}

// Song list on screen: lo-fi first, then the files; files the player cannot
// use (SMPTE timebase, format 2, not MIDI) are marked right away
static void load_songs(void)
{
    s_nsongs = songs_scan(&s_songs);
    s_rows = malloc((size_t)(s_nsongs + 1) * sizeof(char *));
    if (!s_rows) {
        s_nsongs = 0;
        static const char *k_lofi_only[] = { "Lo-fi Generator" };
        ui_set_songs_pinned(k_lofi_only, 1, 1);
        return;
    }
    s_rows[0] = "Lo-fi Generator";
    for (int i = 0; i < s_nsongs; i++) {
        s_rows[i + 1] = s_songs[i];
    }
    ui_set_songs_pinned(s_rows, s_nsongs + 1, 1);

    for (int row = 1; row <= s_nsongs; row++) {
        char path[PATH_MAX_LEN];
        song_path(row, path, sizeof(path));
        esp_err_t err = smf_check(path);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unsupported: %s (%s)", s_songs[row - 1], esp_err_to_name(err));
            ui_set_song_unsupported(row);
        }
    }
}

// ---- Lo-fi piece numbers ----

// esp_timer task: show the piece number on the lo-fi row
static void title_timer_cb(void *arg)
{
    char title[40];
    snprintf(title, sizeof(title), LOFI_TITLE " #%04u", atomic_load(&s_piece));
    ui_set_song_title(LOFI_ROW, title);
}

// Player task, with the player lock held: only store and hand the UI update
// to a timer (a UI call here could deadlock with the LVGL task)
static void on_piece(uint32_t piece)
{
    atomic_store(&s_piece, piece);
    atomic_store(&s_piece_valid, true);
    esp_timer_stop(s_title_timer);   // fine if not running
    esp_timer_start_once(s_title_timer, 1000);
}

// ---- UI callbacks (LVGL task) ----

// Row LOFI_ROW plays lo-fi pieces starting at `piece`; other rows play songs
static void start(int index, uint32_t piece)
{
    if (!atomic_load(&s_connected) || index < 0 || index > s_nsongs) {
        return;
    }
    player_source_t src;
    esp_err_t err;
    if (index == LOFI_ROW) {
        err = lofi_source_create(piece, on_piece, &src);
    } else {
        char path[PATH_MAX_LEN];
        song_path(index, path, sizeof(path));
        err = smf_source_create(path, &src);
        if (err == ESP_ERR_NOT_SUPPORTED || err == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGW(TAG, "unsupported file: %s", s_songs[index - 1]);
            ui_set_song_unsupported(index);
            return;
        }
    }
    if (err == ESP_OK) {
        err = player_start(&src);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "play failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "play row %d, tempo %d%%, transpose %+d", index, s_tempo, s_transpose);
    atomic_store(&s_playing, index);
    ui_set_playing(index);
}

static void on_play(int index)
{
    start(index, esp_random() % LOFI_PIECES);   // lo-fi: a new piece every time
}

// Holding Play: lo-fi replays the piece shown on its row; a song just plays
static void on_play_hold(int index)
{
    if (index == LOFI_ROW && atomic_load(&s_piece_valid)) {
        ESP_LOGI(TAG, "replay piece %04u", atomic_load(&s_piece));
        start(index, atomic_load(&s_piece));
    } else {
        on_play(index);
    }
}

static void on_stop(void)
{
    ESP_LOGI(TAG, "stop");
    player_stop();
    atomic_store(&s_playing, -1);
    ui_set_playing(-1);
}

static void on_tempo_step(int dir)
{
    s_tempo = clamp(s_tempo + dir * UI_TEMPO_STEP, UI_TEMPO_MIN, UI_TEMPO_MAX);
    player_set_tempo_scale(s_tempo);
    ui_set_tempo(s_tempo);
}

static void on_transpose_step(int dir)
{
    s_transpose = clamp(s_transpose + dir, UI_TRANSPOSE_MIN, UI_TRANSPOSE_MAX);
    player_set_transpose(s_transpose);
    ui_set_transpose(s_transpose);
}

// Player task: the song reached its end
static void on_song_end(void)
{
    ESP_LOGI(TAG, "song end");
    atomic_store(&s_playing, -1);
    ui_set_playing(-1);
}

// ---- BLE link (BLE host task) ----

static void on_ble_state(ble_midi_state_t state, const char *name)
{
    static const ui_ble_state_t k_ui[] = {
        [BLE_MIDI_SCANNING] = UI_BLE_SCANNING,
        [BLE_MIDI_CONNECTING] = UI_BLE_CONNECTING,
        [BLE_MIDI_CONNECTED] = UI_BLE_CONNECTED,
        [BLE_MIDI_DISCONNECTED] = UI_BLE_DISCONNECTED,
    };
    atomic_store(&s_connected, state == BLE_MIDI_CONNECTED);
    if (state == BLE_MIDI_DISCONNECTED && atomic_load(&s_playing) >= 0) {
        // Stop playback; the all-off goes out again once the link is back
        ESP_LOGI(TAG, "link lost, stopping");
        player_stop();
        atomic_store(&s_playing, -1);
        ui_set_playing(-1);
    }
    ui_set_ble_status(k_ui[state], name);
}

void app_start(lv_display_t *disp)
{
    static const ui_callbacks_t cb = {
        .on_play = on_play,
        .on_stop = on_stop,
        .on_tempo_step = on_tempo_step,
        .on_transpose_step = on_transpose_step,
        .on_play_hold = on_play_hold,
    };
    const esp_timer_create_args_t targs = {
        .callback = title_timer_cb,
        .name = "lofi_title",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_title_timer));
    ESP_ERROR_CHECK(player_init(on_song_end));
    player_set_tempo_scale(s_tempo);
    player_set_transpose(s_transpose);

    ui_init(disp, &cb);
    ui_set_tempo(s_tempo);
    ui_set_transpose(s_transpose);
    ui_set_ble_status(UI_BLE_SCANNING, NULL);

    if (songs_mount() != ESP_OK) {
        ESP_LOGE(TAG, "song storage not available, lo-fi only");
    }
    load_songs();

    ESP_ERROR_CHECK(ble_midi_start(on_ble_state));
}
