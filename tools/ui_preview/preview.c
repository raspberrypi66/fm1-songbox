// Host preview: runs the real main/ui/ui.c on LVGL with a 320x240 RGB565
// framebuffer and writes one PPM per scenario. render.py turns them into PNGs.
//
// Time is simulated, so animation frames (EQ bars, status pulse) are
// deterministic between runs.

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"

#define W 320
#define H 240

static uint32_t s_ms;
static uint16_t s_fb[W * H];
static uint8_t s_draw_buf[W * 40 * 2];
static const char *s_out_dir = ".";

static uint32_t tick_cb(void)
{
    return s_ms;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) {
        memcpy(&s_fb[y * W + a->x1], px + (size_t)(y - a->y1) * w * 2, (size_t)w * 2);
    }
    lv_display_flush_ready(d);
}

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_ms += 5;
        lv_timer_handler();
    }
}

static void snapshot(const char *name)
{
    lv_refr_now(NULL);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", s_out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = s_fb[i];
        uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
        uint8_t rgb[3] = { (uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)),
                           (uint8_t)((b << 3) | (b >> 2)) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}

// Same names as main/demo.c
static const char *const k_songs[] = {
    "Clair de Lune.mid",
    "Take Five.mid",
    "Bohemian Rhapsody - Full Band Arrangement.mid",
    "Moonlight Sonata 1st Movement.mid",
    "Super Mario Bros - Overworld Theme (Koji Kondo).mid",
    "smpte_timecode_test.mid",
    "Canon in D.MID",
    "The Entertainer.mid",
};
#define SONG_COUNT ((int)(sizeof(k_songs) / sizeof(k_songs[0])))
#define BAD_SONG 5

static lv_obj_t *list_obj(void)
{
    // Screen children: top bar, list, empty label, EQ, dock (see ui.c build order)
    return lv_obj_get_child(lv_screen_active(), 1);
}

// Tap a row through the real event path (bubbles to the list handler)
static void tap_row(int i)
{
    lv_obj_t *row = lv_obj_get_child(list_obj(), i);
    lv_obj_send_event(row, LV_EVENT_CLICKED, NULL);
    run_ms(400);
}

static void reset(void)
{
    ui_set_songs(NULL, 0);
    ui_set_songs(k_songs, SONG_COUNT);
    ui_set_tempo(100);
    ui_set_transpose(0);
    ui_set_playing(-1);
    lv_obj_scroll_to_y(list_obj(), 0, LV_ANIM_OFF);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        s_out_dir = argv[1];
    }
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_draw_buf, NULL, sizeof(s_draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    ui_init(disp, NULL);
    run_ms(50);

    // (a) Connected, song 2 playing and selected, tempo 110 %, transpose +2
    reset();
    ui_set_ble_status(UI_BLE_CONNECTED, "FM-1");
    ui_set_song_unsupported(BAD_SONG);
    tap_row(1);
    ui_set_playing(1);
    ui_set_tempo(110);
    ui_set_transpose(2);
    run_ms(780);   // a representative EQ frame
    snapshot("a_connected_playing");

    // (b) Scanning, nothing playing, Play disabled, defaults
    reset();
    ui_set_ble_status(UI_BLE_SCANNING, NULL);
    run_ms(400);
    snapshot("b_scanning_play_disabled");

    // (c) Connected, idle, list scrolled to the unsupported song, Canon selected
    reset();
    ui_set_ble_status(UI_BLE_CONNECTED, "FM-1");
    ui_set_song_unsupported(BAD_SONG);
    lv_obj_scroll_to_y(list_obj(), 4 * 36, LV_ANIM_OFF);
    tap_row(6);
    ui_set_transpose(-3);
    run_ms(300);
    snapshot("c_unsupported_file");

    // (d) Playing song 2, then the user tapped another song: only the selection moves
    reset();
    ui_set_ble_status(UI_BLE_CONNECTED, "FM-1");
    tap_row(1);
    ui_set_playing(1);
    tap_row(3);
    ui_set_tempo(95);
    run_ms(380);   // EQ mid-motion; marquee still resting on the title start (tap + 780 ms < 1200 ms)
    snapshot("d_playing_other_selected");

    // (e) Disconnected, ranges at their limits (+ and - greyed out)
    reset();
    ui_set_ble_status(UI_BLE_DISCONNECTED, NULL);
    ui_set_tempo(150);
    ui_set_transpose(-12);
    run_ms(300);
    snapshot("e_disconnected_limits");

    // (f) Connecting, empty song list
    ui_set_songs(NULL, 0);
    ui_set_tempo(100);
    ui_set_transpose(0);
    ui_set_ble_status(UI_BLE_CONNECTING, NULL);
    run_ms(300);
    snapshot("f_connecting_no_songs");

    return 0;
}
