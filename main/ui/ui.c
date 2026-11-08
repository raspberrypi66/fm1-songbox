// FM-1 Song Box screen. See ui.h for the API contract and ui_theme.h for tokens.
//
// Layout (320x240 landscape):
//
//   y=0   +--------------------------------------------------------------+
//         | [logo] SONG BOX                        ( * Connected  FM-1 ) |  top bar 28
//   y=28  +--------------------------------------------------------------+
//         |  01   Moonlight Sonata                                       |
//         | (|||  Take Five                                      card )  |  list 144
//         |  03   Bohemian Rhapsody - Full Band Arr...                   |  = 4 rows x 36
//         |  !    smpte_test                      Unsupported file       |
//   y=172 +--------------------------------------------------------------+
//         |      TEMPO              ____             TRANSPOSE           |
//         |  ( -   110%   + )     ( >  )       ( -    +2    + )          |  dock 68
//         |                        ----                                  |
//   y=240 +--------------------------------------------------------------+
//
// Row states map onto LVGL states, so styles do the work and LVGL's numeric
// state precedence resolves the combinations:
//   CHECKED  = selected  (raised card)          weight 0x0001
//   PRESSED  = finger down (lighter card)        weight 0x0020
//   USER_1   = playing   (accent text + EQ)      weight 0x1000
//   USER_2   = unsupported (dim text + warning)  weight 0x2000
// Only CHECKED and PRESSED set a background, so pressing always gives feedback.

#include "ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "assets/ui_assets.h"
#include "ui_theme.h"

#ifdef ESP_PLATFORM
#include "esp_lvgl_port.h"
#define UI_LOCK()   lvgl_port_lock(0)
#define UI_UNLOCK() lvgl_port_unlock()
#else
// Host preview build (tools/ui_preview): single thread, no lock
#define UI_LOCK()   (true)
#define UI_UNLOCK() ((void)0)
#endif

#define C(hex) lv_color_hex(hex)

#define ROW_STATE_PLAYING LV_STATE_USER_1
#define ROW_STATE_BAD     LV_STATE_USER_2

#define EQ_BARS   3
#define EQ_BAR_W  3
#define EQ_GAP    2
#define EQ_W      (EQ_BARS * EQ_BAR_W + (EQ_BARS - 1) * EQ_GAP)
#define EQ_H      14
#define EQ_MIN_H  4

enum { STEP_TEMPO_DOWN = 1, STEP_TEMPO_UP, STEP_TR_DOWN, STEP_TR_UP };

typedef struct {
    lv_obj_t *row;
    lv_obj_t *idx;     // track number, or warning glyph when unsupported
    lv_obj_t *name;
    lv_obj_t *trail;   // "Unsupported file", created on demand
    bool bad;
} ui_row_t;

typedef struct {
    lv_obj_t *minus;
    lv_obj_t *plus;
    lv_obj_t *value;
    lv_obj_t *unit;    // "%" for tempo, NULL for transpose
} ui_stepper_t;

static struct {
    ui_callbacks_t cb;

    lv_obj_t *scr;
    lv_obj_t *list;
    lv_obj_t *empty;
    lv_obj_t *chip;
    lv_obj_t *chip_icon;
    lv_obj_t *chip_text;
    lv_obj_t *chip_name;
    lv_obj_t *play;
    lv_obj_t *play_icon;
    lv_obj_t *eq;
    lv_obj_t *eq_bar[EQ_BARS];
    ui_stepper_t tempo;
    ui_stepper_t transpose;

    ui_row_t *rows;
    int count;
    int selected;
    int playing;
    ui_ble_state_t ble;
} s;

// ---------------------------------------------------------------------------
// Styles (shared, so each row costs no per-object style memory)
// ---------------------------------------------------------------------------

static lv_style_t st_row, st_row_pressed, st_row_sel, st_row_play, st_row_sel_play, st_row_bad;
static lv_style_t st_name, st_idx, st_caption;
static lv_anim_t marquee_tpl;
static lv_style_t st_pill, st_step_btn, st_step_btn_pr, st_step_btn_dis;
static lv_style_t st_play, st_play_pr, st_play_dis;
static lv_style_t st_scrollbar;

static void styles_init(void)
{
    // List row: transparent until selected or pressed. Border always 1 px wide
    // (transparent at rest) so the content box never shifts between states.
    lv_style_init(&st_row);
    lv_style_set_width(&st_row, lv_pct(100));
    lv_style_set_height(&st_row, UI_ROW_H);
    lv_style_set_radius(&st_row, UI_RADIUS_CARD);
    lv_style_set_bg_opa(&st_row, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_row, 1);
    lv_style_set_border_opa(&st_row, LV_OPA_TRANSP);
    lv_style_set_pad_left(&st_row, 0);
    lv_style_set_pad_right(&st_row, UI_SP_3);
    lv_style_set_pad_column(&st_row, UI_SP_1);
    lv_style_set_text_color(&st_row, C(UI_C_TEXT_2));   // inherited by the name label
    lv_style_set_text_font(&st_row, UI_FONT_BODY);
    lv_style_set_layout(&st_row, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&st_row, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_cross_place(&st_row, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&st_row, LV_FLEX_ALIGN_CENTER);

    lv_style_init(&st_row_sel);
    lv_style_set_bg_opa(&st_row_sel, LV_OPA_COVER);
    lv_style_set_bg_color(&st_row_sel, C(UI_C_SURFACE));
    lv_style_set_border_opa(&st_row_sel, LV_OPA_COVER);
    lv_style_set_border_color(&st_row_sel, C(UI_C_BORDER));
    lv_style_set_text_color(&st_row_sel, C(UI_C_TEXT));

    lv_style_init(&st_row_pressed);
    lv_style_set_bg_opa(&st_row_pressed, LV_OPA_COVER);
    lv_style_set_bg_color(&st_row_pressed, C(UI_C_SURFACE_HI));

    lv_style_init(&st_row_play);
    lv_style_set_text_color(&st_row_play, C(UI_C_ACCENT_TEXT));

    // Selected and playing: the card picks up an accent edge
    lv_style_init(&st_row_sel_play);
    lv_style_set_border_color(&st_row_sel_play, C(UI_C_ACCENT));
    lv_style_set_border_opa(&st_row_sel_play, LV_OPA_60);

    lv_style_init(&st_row_bad);
    lv_style_set_text_color(&st_row_bad, C(UI_C_TEXT_3));

    // Song name: one line, takes the remaining width, colour inherited from the row.
    // When selected it becomes a marquee that rests on the beginning of the
    // title first (delay) and again after every pass (repeat delay).
    lv_anim_init(&marquee_tpl);
    lv_anim_set_delay(&marquee_tpl, 1200);
    lv_anim_set_repeat_delay(&marquee_tpl, 1500);
    lv_anim_set_repeat_count(&marquee_tpl, LV_ANIM_REPEAT_INFINITE);
    lv_style_init(&st_name);
    lv_style_set_height(&st_name, lv_font_get_line_height(UI_FONT_BODY));   // one line: truncate, never wrap
    lv_style_set_flex_grow(&st_name, 1);
    lv_style_set_anim(&st_name, &marquee_tpl);

    // Track number column
    lv_style_init(&st_idx);
    lv_style_set_width(&st_idx, UI_ROW_SLOT_W);
    lv_style_set_text_align(&st_idx, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_font(&st_idx, UI_FONT_CAPTION);
    lv_style_set_text_color(&st_idx, C(UI_C_TEXT_3));

    // Small caps captions ("TEMPO", "TRANSPOSE", wordmark)
    lv_style_init(&st_caption);
    lv_style_set_text_font(&st_caption, UI_FONT_CAPTION);
    lv_style_set_text_color(&st_caption, C(UI_C_TEXT_3));
    lv_style_set_text_letter_space(&st_caption, UI_CAPTION_TRACK);

    // Stepper pill and its - / + segments. The segments are circles that
    // exactly fill the pill's round ends, so the pressed state looks native.
    lv_style_init(&st_pill);
    lv_style_set_size(&st_pill, UI_STEP_W, UI_STEP_H);
    lv_style_set_radius(&st_pill, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&st_pill, LV_OPA_COVER);
    lv_style_set_bg_color(&st_pill, C(UI_C_SURFACE));
    lv_style_set_border_width(&st_pill, 1);
    lv_style_set_border_color(&st_pill, C(UI_C_BORDER));
    lv_style_set_layout(&st_pill, LV_LAYOUT_FLEX);
    lv_style_set_flex_flow(&st_pill, LV_FLEX_FLOW_ROW);
    lv_style_set_flex_cross_place(&st_pill, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_track_place(&st_pill, LV_FLEX_ALIGN_CENTER);

    lv_style_init(&st_step_btn);
    lv_style_set_size(&st_step_btn, UI_STEP_BTN_W, UI_STEP_BTN_H);
    lv_style_set_radius(&st_step_btn, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&st_step_btn, LV_OPA_TRANSP);
    lv_style_set_bg_color(&st_step_btn, C(UI_C_SURFACE_HI));
    lv_style_set_text_color(&st_step_btn, C(UI_C_TEXT_2));
    lv_style_set_text_font(&st_step_btn, UI_FONT_BODY);

    lv_style_init(&st_step_btn_pr);
    lv_style_set_bg_opa(&st_step_btn_pr, LV_OPA_COVER);
    lv_style_set_text_color(&st_step_btn_pr, C(UI_C_TEXT));

    lv_style_init(&st_step_btn_dis);
    lv_style_set_text_color(&st_step_btn_dis, C(UI_C_TEXT_DIS));

    // Play / Stop: the one accent object on screen, with a soft glow
    lv_style_init(&st_play);
    lv_style_set_size(&st_play, UI_PLAY_D, UI_PLAY_D);
    lv_style_set_radius(&st_play, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&st_play, LV_OPA_COVER);
    lv_style_set_bg_color(&st_play, C(UI_C_ACCENT_TOP));
    lv_style_set_bg_grad_color(&st_play, C(UI_C_ACCENT_BOT));
    lv_style_set_bg_grad_dir(&st_play, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&st_play, 1);
    lv_style_set_border_color(&st_play, C(UI_C_ACCENT_TOP));
    lv_style_set_border_opa(&st_play, LV_OPA_TRANSP);
    lv_style_set_shadow_width(&st_play, 18);
    lv_style_set_shadow_offset_y(&st_play, 3);
    lv_style_set_shadow_color(&st_play, C(UI_C_ACCENT));
    lv_style_set_shadow_opa(&st_play, LV_OPA_50);

    lv_style_init(&st_play_pr);
    lv_style_set_bg_color(&st_play_pr, C(UI_C_ACCENT_PR_T));
    lv_style_set_bg_grad_color(&st_play_pr, C(UI_C_ACCENT_PR_B));
    lv_style_set_shadow_opa(&st_play_pr, LV_OPA_20);
    lv_style_set_shadow_width(&st_play_pr, 10);

    lv_style_init(&st_play_dis);
    lv_style_set_bg_color(&st_play_dis, C(UI_C_SURFACE));
    lv_style_set_bg_grad_dir(&st_play_dis, LV_GRAD_DIR_NONE);
    lv_style_set_border_color(&st_play_dis, C(UI_C_BORDER));
    lv_style_set_border_opa(&st_play_dis, LV_OPA_COVER);
    lv_style_set_shadow_opa(&st_play_dis, LV_OPA_TRANSP);

    // Thin scrollbar: shows there is more below without stealing attention
    lv_style_init(&st_scrollbar);
    lv_style_set_width(&st_scrollbar, 3);
    lv_style_set_radius(&st_scrollbar, 2);
    lv_style_set_bg_opa(&st_scrollbar, LV_OPA_COVER);
    lv_style_set_bg_color(&st_scrollbar, C(UI_C_BORDER));
    lv_style_set_pad_right(&st_scrollbar, 2);
    lv_style_set_pad_top(&st_scrollbar, 4);
    lv_style_set_pad_bottom(&st_scrollbar, 4);

#if UI_USE_SKIN
    // Skin: the plate supplies the bars, wells and socket; bitmaps replace
    // the drawn card and button faces. Borders stay 1 px wide (transparent)
    // so the geometry is identical to the flat theme.
    lv_style_set_bg_opa(&st_row_sel, LV_OPA_TRANSP);
    lv_style_set_border_opa(&st_row_sel, LV_OPA_TRANSP);
    lv_style_set_bg_image_src(&st_row_sel, &img_skin_card);
    lv_style_set_border_opa(&st_row_sel_play, LV_OPA_TRANSP);
    lv_style_set_bg_image_src(&st_row_sel_play, &img_skin_card_live);
    lv_style_set_text_color(&st_row_sel_play, C(UI_C_ACCENT_PALE));   // keeps contrast on the violet card
    lv_style_set_bg_color(&st_row_pressed, lv_color_white());
    lv_style_set_bg_opa(&st_row_pressed, 18);

    lv_style_set_bg_opa(&st_pill, LV_OPA_TRANSP);
    lv_style_set_border_opa(&st_pill, LV_OPA_TRANSP);
    lv_style_set_bg_color(&st_step_btn, lv_color_white());
    lv_style_set_bg_opa(&st_step_btn_pr, 30);

    lv_style_set_bg_opa(&st_play, LV_OPA_TRANSP);
    lv_style_set_bg_grad_dir(&st_play, LV_GRAD_DIR_NONE);
    lv_style_set_bg_image_src(&st_play, &img_skin_orb);
    lv_style_set_bg_image_src(&st_play_pr, &img_skin_orb_pr);
    lv_style_set_bg_opa(&st_play_dis, LV_OPA_TRANSP);
    lv_style_set_border_opa(&st_play_dis, LV_OPA_TRANSP);
    lv_style_set_bg_image_src(&st_play_dis, &img_skin_orb_off);
#endif
}

// A bare container: no theme styles, not clickable, not scrollable
static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const char *str)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, str);
    return l;
}

// ---------------------------------------------------------------------------
// Animations
// ---------------------------------------------------------------------------

static int32_t sin01(int32_t deg, int32_t scale)   // (sin + 1) / 2 * scale
{
    int32_t v = lv_trigo_sin((int16_t)(deg % 360));
    return ((v + LV_TRIGO_SIN_MAX) * scale) / (2 * LV_TRIGO_SIN_MAX);
}

// Three bars driven by two sines each with integer frequencies, so the
// 360-degree loop is seamless and the motion looks organic, not mechanical.
static void eq_anim_cb(void *var, int32_t v)
{
    LV_UNUSED(var);
    static const uint8_t f1[EQ_BARS] = { 2, 3, 2 };
    static const uint8_t f2[EQ_BARS] = { 5, 4, 7 };
    static const int16_t ph[EQ_BARS] = { 0, 140, 260 };
    const int32_t span = EQ_H - EQ_MIN_H;
    for (int i = 0; i < EQ_BARS; i++) {
        int32_t a = sin01(f1[i] * v + ph[i], 600);
        int32_t b = sin01(f2[i] * v + ph[i] * 2, 400);
        int32_t h = EQ_MIN_H + ((a + b) * span) / 1000;
        lv_obj_set_y(s.eq_bar[i], EQ_H - h);
        lv_obj_set_height(s.eq_bar[i], h);
    }
}

static void eq_start(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s.eq);
    lv_anim_set_exec_cb(&a, eq_anim_cb);
    lv_anim_set_values(&a, 0, 359);
    lv_anim_set_duration(&a, 2400);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void eq_stop(void)
{
    lv_anim_delete(s.eq, eq_anim_cb);
}

static void pulse_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_text_opa(var, (lv_opa_t)(80 + sin01(v + 90, 175)), 0);
}

static void pulse_set(uint32_t period_ms)
{
    lv_anim_delete(s.chip_icon, pulse_anim_cb);
    lv_obj_set_style_text_opa(s.chip_icon, LV_OPA_COVER, 0);
    if (period_ms == 0) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s.chip_icon);
    lv_anim_set_exec_cb(&a, pulse_anim_cb);
    lv_anim_set_values(&a, 0, 359);
    lv_anim_set_duration(&a, period_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

// ---------------------------------------------------------------------------
// State -> view
// ---------------------------------------------------------------------------

static bool row_valid(int i)
{
    return i >= 0 && i < s.count;
}

static void play_button_update(void)
{
    bool playing = row_valid(s.playing);
    bool can_play = s.ble == UI_BLE_CONNECTED && row_valid(s.selected) && !s.rows[s.selected].bad;
    bool enabled = playing || can_play;

    // Only touch what changed: each change redraws part of the glowing button
    const void *src = playing ? &img_icon_stop : &img_icon_play;
    if (lv_image_get_src(s.play_icon) != src) {
        lv_image_set_src(s.play_icon, src);
    }
    if (enabled) {
        lv_obj_remove_state(s.play, LV_STATE_DISABLED);   // no-op if unchanged
    } else {
        lv_obj_add_state(s.play, LV_STATE_DISABLED);
    }
    lv_color_t tint = C(enabled ? UI_C_TEXT : UI_C_ICON_DIS);
    if (!lv_color_eq(lv_obj_get_style_image_recolor(s.play_icon, LV_PART_MAIN), tint)) {
        lv_obj_set_style_image_recolor(s.play_icon, tint, 0);
    }
}

static void chip_update(const char *name)
{
    uint32_t color;
    const char *label;
    uint32_t pulse_ms = 0;

    switch (s.ble) {
    case UI_BLE_CONNECTING:
        color = UI_C_WARN;
        label = "Connecting";
        pulse_ms = 700;
        break;
    case UI_BLE_CONNECTED:
        color = UI_C_OK;
        label = "Connected";
        break;
    case UI_BLE_DISCONNECTED:
        color = UI_C_ERR;
        label = "Disconnected";
        break;
    case UI_BLE_SCANNING:
    default:
        color = UI_C_WARN;
        label = "Scanning";
        pulse_ms = 1600;
        break;
    }

    lv_obj_set_style_bg_color(s.chip, C(color), 0);
    lv_obj_set_style_border_color(s.chip, C(color), 0);
    lv_obj_set_style_text_color(s.chip, C(color), 0);   // icon + status word inherit
    lv_label_set_text(s.chip_text, label);

    bool show_name = s.ble == UI_BLE_CONNECTED && name && name[0];
    if (show_name) {
        lv_label_set_text(s.chip_name, name);
        lv_obj_remove_flag(s.chip_name, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s.chip_name, LV_OBJ_FLAG_HIDDEN);
    }
    pulse_set(pulse_ms);
}

static void stepper_show(ui_stepper_t *st, int value, int min, int max, int def, bool show_sign)
{
    char buf[8];
    if (show_sign && value != 0) {
        snprintf(buf, sizeof(buf), "%+d", value);   // "+3", "-2"
    } else {
        snprintf(buf, sizeof(buf), "%d", value);
    }
    lv_label_set_text(st->value, buf);
    // Non-default values take the accent, so "this song is altered" reads at a glance
    lv_obj_set_style_text_color(st->value, C(value == def ? UI_C_TEXT : UI_C_ACCENT_TEXT), 0);

    if (value <= min) {
        lv_obj_add_state(st->minus, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(st->minus, LV_STATE_DISABLED);
    }
    if (value >= max) {
        lv_obj_add_state(st->plus, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(st->plus, LV_STATE_DISABLED);
    }
}

static void tempo_show(int percent)
{
    stepper_show(&s.tempo, percent, UI_TEMPO_MIN, UI_TEMPO_MAX, UI_TEMPO_DEFAULT, false);
}

static void transpose_show(int semitones)
{
    stepper_show(&s.transpose, semitones, UI_TRANSPOSE_MIN, UI_TRANSPOSE_MAX, UI_TRANSPOSE_DEFAULT, true);
}

// Scroll the list just enough to show row i fully (no jump if already visible).
// Rows are 36 px and the viewport is 4 rows, so results stay snap-aligned.
static void list_reveal(int i, lv_anim_enable_t anim)
{
    if (!row_valid(i)) {
        return;
    }
    int32_t top = (int32_t)i * UI_ROW_H;
    int32_t y = lv_obj_get_scroll_y(s.list);
    if (top < y) {
        y = top;
    } else if (top + UI_ROW_H > y + UI_LIST_H) {
        y = top + UI_ROW_H - UI_LIST_H;
    } else {
        return;
    }
    int32_t max = (int32_t)s.count * UI_ROW_H - UI_LIST_H;
    y = LV_CLAMP(0, y, max > 0 ? max : 0);
    lv_obj_scroll_to_y(s.list, y, anim);
}

static void select_row(int i)
{
    if (row_valid(s.selected)) {
        lv_obj_remove_state(s.rows[s.selected].row, LV_STATE_CHECKED);
        lv_label_set_long_mode(s.rows[s.selected].name, LV_LABEL_LONG_MODE_DOTS);
    }
    s.selected = row_valid(i) ? i : -1;
    if (row_valid(s.selected)) {
        ui_row_t *r = &s.rows[s.selected];
        lv_obj_add_state(r->row, LV_STATE_CHECKED);
        // The selected title scrolls if it does not fit, so long names stay readable
        lv_label_set_long_mode(r->name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    }
    play_button_update();
}

static void playing_apply(int i)
{
    if (row_valid(s.playing)) {
        ui_row_t *r = &s.rows[s.playing];
        lv_obj_remove_state(r->row, ROW_STATE_PLAYING);
        lv_obj_set_style_text_opa(r->idx, LV_OPA_COVER, 0);
    }
    eq_stop();
    lv_obj_add_flag(s.eq, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_parent(s.eq, s.scr);

    s.playing = row_valid(i) ? i : -1;
    if (row_valid(s.playing)) {
        ui_row_t *r = &s.rows[s.playing];
        lv_obj_add_state(r->row, ROW_STATE_PLAYING);
        if (!r->bad) {
            // The EQ takes the track-number slot. The number stays in the
            // layout (only made invisible) so the name does not shift left.
            lv_obj_set_style_text_opa(r->idx, LV_OPA_TRANSP, 0);
            lv_obj_set_parent(s.eq, r->row);
            lv_obj_align(s.eq, LV_ALIGN_LEFT_MID, (UI_ROW_SLOT_W - EQ_W) / 2, 0);
            lv_obj_remove_flag(s.eq, LV_OBJ_FLAG_HIDDEN);
            eq_start();
        }
    }
    play_button_update();
}

static void row_mark_bad(int i)
{
    ui_row_t *r = &s.rows[i];
    if (r->bad) {
        return;
    }
    r->bad = true;
    lv_obj_add_state(r->row, ROW_STATE_BAD);
    lv_label_set_text(r->idx, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(r->idx, C(UI_C_ERR), 0);
    lv_obj_set_style_text_opa(r->idx, LV_OPA_COVER, 0);

    r->trail = text(r->row, "Unsupported file");
    lv_obj_set_style_text_font(r->trail, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(r->trail, C(UI_C_ERR), 0);

    if (s.playing == i) {
        playing_apply(-1);
    } else {
        play_button_update();
    }
}

// "Song.mid" / "SONG.MIDI" -> "Song" (display only; FAT may upper-case extensions)
static void display_name(char *dst, size_t len, const char *src)
{
    snprintf(dst, len, "%s", src ? src : "");
    size_t n = strlen(dst);
    static const char *const ext[] = { ".mid", ".midi" };
    for (size_t k = 0; k < sizeof(ext) / sizeof(ext[0]); k++) {
        size_t e = strlen(ext[k]);
        if (n <= e) {
            continue;
        }
        bool match = true;
        for (size_t j = 0; match && j < e; j++) {
            char c = dst[n - e + j];
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
            match = c == ext[k][j];
        }
        if (match) {
            dst[n - e] = '\0';
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Events (run in the LVGL task, lock held)
// ---------------------------------------------------------------------------

static void list_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target_obj(e);
    if (lv_obj_get_parent(target) != s.list) {
        return;   // a tap on the empty list area
    }
    int i = (int)lv_obj_get_index(target);
    if (i != s.selected) {
        select_row(i);   // with scroll snap, a row at rest is always fully visible
    }
}

// Short tap: Play / Stop. Holding Play (while stopped): the replay callback.
// Short-clicked rather than clicked, so a long press never also plays or stops.
static void play_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        if (!row_valid(s.playing) && !lv_obj_has_state(s.play, LV_STATE_DISABLED) && row_valid(s.selected) &&
            s.cb.on_play_hold) {
            s.cb.on_play_hold(s.selected);
        }
        return;
    }
    if (row_valid(s.playing)) {
        if (s.cb.on_stop) {
            s.cb.on_stop();
        }
    } else if (!lv_obj_has_state(s.play, LV_STATE_DISABLED) && row_valid(s.selected)) {
        if (s.cb.on_play) {
            s.cb.on_play(s.selected);
        }
    }
}

// Steps on press (not release) so it feels immediate; holding repeats.
static void step_event_cb(lv_event_t *e)
{
    switch ((intptr_t)lv_event_get_user_data(e)) {
    case STEP_TEMPO_DOWN:
        if (s.cb.on_tempo_step) s.cb.on_tempo_step(-1);
        break;
    case STEP_TEMPO_UP:
        if (s.cb.on_tempo_step) s.cb.on_tempo_step(+1);
        break;
    case STEP_TR_DOWN:
        if (s.cb.on_transpose_step) s.cb.on_transpose_step(-1);
        break;
    case STEP_TR_UP:
        if (s.cb.on_transpose_step) s.cb.on_transpose_step(+1);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

static void build_topbar(lv_obj_t *scr)
{
    lv_obj_t *bar = box(scr);
    lv_obj_set_size(bar, 320, UI_TOPBAR_H);
    lv_obj_set_style_bg_opa(bar, UI_USE_SKIN ? LV_OPA_TRANSP : LV_OPA_COVER, 0);   // skin: plate draws it
    lv_obj_set_style_bg_color(bar, C(UI_C_CHROME), 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_opa(bar, UI_USE_SKIN ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, C(UI_C_HAIRLINE), 0);

    lv_obj_t *logo = lv_image_create(bar);
    lv_image_set_src(logo, &img_logo_mark);
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, UI_EDGE, 0);

    lv_obj_t *word = text(bar, "SONG BOX");
    lv_obj_add_style(word, &st_caption, 0);
    lv_obj_set_style_text_color(word, C(UI_C_TEXT_2), 0);
    lv_obj_set_style_text_letter_space(word, 2, 0);
    lv_obj_align(word, LV_ALIGN_LEFT_MID, UI_EDGE + 18 + UI_SP_2, 0);

    // Status chip: tinted pill, colour = meaning, text says the same thing
    s.chip = box(bar);
    lv_obj_set_size(s.chip, LV_SIZE_CONTENT, UI_CHIP_H);
    lv_obj_set_style_radius(s.chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s.chip, 36, 0);
    lv_obj_set_style_border_width(s.chip, 1, 0);
    lv_obj_set_style_border_opa(s.chip, 90, 0);
    lv_obj_set_style_pad_left(s.chip, 7, 0);
    lv_obj_set_style_pad_right(s.chip, 9, 0);
    lv_obj_set_style_pad_column(s.chip, 5, 0);
    lv_obj_set_style_text_font(s.chip, UI_FONT_CAPTION, 0);
    lv_obj_set_flex_flow(s.chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s.chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s.chip, LV_ALIGN_RIGHT_MID, -UI_EDGE, 0);

    s.chip_icon = text(s.chip, LV_SYMBOL_BLUETOOTH);
    s.chip_text = text(s.chip, "");
    s.chip_name = text(s.chip, "");
    lv_obj_set_style_text_color(s.chip_name, C(UI_C_TEXT), 0);
    lv_obj_set_style_pad_left(s.chip_name, 1, 0);
}

static void build_list(lv_obj_t *scr)
{
    s.list = box(scr);
    lv_obj_add_flag(s.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s.list, 0, UI_TOPBAR_H);
    lv_obj_set_size(s.list, 320, UI_LIST_H);
    lv_obj_set_style_pad_hor(s.list, UI_EDGE, 0);
    lv_obj_set_flex_flow(s.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s.list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(s.list, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(s.list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_style(s.list, &st_scrollbar, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(s.list, list_event_cb, LV_EVENT_CLICKED, NULL);

    // Empty state lives outside the list so list children are exactly the rows
    s.empty = text(scr, "No songs");
    lv_obj_set_style_text_font(s.empty, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(s.empty, C(UI_C_TEXT_3), 0);
    lv_obj_align(s.empty, LV_ALIGN_TOP_MID, 0, UI_TOPBAR_H + UI_LIST_H / 2 - 9);

    // Playing indicator, moved into the playing row's slot
    s.eq = box(scr);
    lv_obj_add_flag(s.eq, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s.eq, EQ_W, EQ_H);
    for (int i = 0; i < EQ_BARS; i++) {
        lv_obj_t *b = box(s.eq);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, C(UI_C_ACCENT_TEXT), 0);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_pos(b, i * (EQ_BAR_W + EQ_GAP), EQ_H - EQ_MIN_H);
        lv_obj_set_size(b, EQ_BAR_W, EQ_MIN_H);
        s.eq_bar[i] = b;
    }
    eq_anim_cb(NULL, 40);   // a pleasant static frame until the animation runs
}

static lv_obj_t *step_button(lv_obj_t *pill, const char *symbol, intptr_t code)
{
    lv_obj_t *b = box(pill);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(b, &st_step_btn, 0);
    lv_obj_add_style(b, &st_step_btn_pr, LV_STATE_PRESSED);
    lv_obj_add_style(b, &st_step_btn_dis, LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(b, 2);
    lv_obj_add_event_cb(b, step_event_cb, LV_EVENT_PRESSED, (void *)code);
    lv_obj_add_event_cb(b, step_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, (void *)code);
    lv_obj_center(text(b, symbol));
    return b;
}

static lv_obj_t *build_stepper(lv_obj_t *dock, const char *caption, ui_stepper_t *st, bool percent, intptr_t down,
                               intptr_t up)
{
    lv_obj_t *group = box(dock);
    lv_obj_set_size(group, UI_STEP_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 3, 0);

    lv_obj_t *cap = text(group, caption);
    lv_obj_add_style(cap, &st_caption, 0);
    lv_obj_set_style_text_color(cap, C(UI_C_DOCK_CAPTION), 0);

    lv_obj_t *pill = box(group);
    lv_obj_add_style(pill, &st_pill, 0);

    st->minus = step_button(pill, LV_SYMBOL_MINUS, down);

    // Value: big number, small muted unit, sharing a baseline
    lv_obj_t *val = box(pill);
    lv_obj_set_height(val, UI_STEP_BTN_H);
    lv_obj_set_flex_grow(val, 1);
    lv_obj_set_flex_flow(val, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(val, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_bottom(val, (UI_STEP_BTN_H - 22) / 2, 0);
    lv_obj_set_style_pad_column(val, 1, 0);

    st->value = text(val, "");
    lv_obj_set_style_text_font(st->value, UI_FONT_VALUE, 0);
    st->unit = NULL;
    if (percent) {
        st->unit = text(val, "%");
        lv_obj_add_style(st->unit, &st_caption, 0);
        lv_obj_set_style_text_letter_space(st->unit, 0, 0);
        lv_obj_set_style_translate_y(st->unit, -1, 0);   // 12 px baseline sits 1 px lower than 20 px
    }

    st->plus = step_button(pill, LV_SYMBOL_PLUS, up);
    return group;
}

static void build_dock(lv_obj_t *scr)
{
    lv_obj_t *dock = box(scr);
    lv_obj_set_pos(dock, 0, 240 - UI_DOCK_H);
    lv_obj_set_size(dock, 320, UI_DOCK_H);
    lv_obj_set_style_bg_opa(dock, UI_USE_SKIN ? LV_OPA_TRANSP : LV_OPA_COVER, 0);   // skin: plate draws it
    lv_obj_set_style_bg_color(dock, C(UI_C_CHROME), 0);
    lv_obj_set_style_border_side(dock, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(dock, 1, 0);
    lv_obj_set_style_border_opa(dock, UI_USE_SKIN ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dock, C(UI_C_HAIRLINE), 0);
    lv_obj_set_style_pad_hor(dock, UI_EDGE, 0);
    lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    build_stepper(dock, "TEMPO", &s.tempo, true, STEP_TEMPO_DOWN, STEP_TEMPO_UP);

    // Play / Stop in the centre: the focal point, symmetric between the steppers
    s.play = box(dock);
    lv_obj_add_flag(s.play, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(s.play, &st_play, 0);
    lv_obj_add_style(s.play, &st_play_pr, LV_STATE_PRESSED);
    lv_obj_add_style(s.play, &st_play_dis, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s.play, play_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(s.play, play_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    s.play_icon = lv_image_create(s.play);
    lv_obj_set_style_image_recolor_opa(s.play_icon, LV_OPA_COVER, 0);
    lv_obj_center(s.play_icon);

    build_stepper(dock, "TRANSPOSE", &s.transpose, false, STEP_TR_DOWN, STEP_TR_UP);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ui_init(lv_display_t *disp, const ui_callbacks_t *cb)
{
    if (!UI_LOCK()) {
        return;
    }
    memset(&s, 0, sizeof(s));
    if (cb) {
        s.cb = *cb;
    }
    s.selected = -1;
    s.playing = -1;
    s.ble = UI_BLE_SCANNING;

    styles_init();
    if (disp) {
        lv_display_set_default(disp);
    }
    lv_obj_t *old = lv_screen_active();

    s.scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s.scr);
    lv_obj_remove_flag(s.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s.scr, C(UI_C_BG), 0);
#if UI_USE_SKIN
    lv_obj_set_style_bg_image_src(s.scr, &img_skin_bg, 0);
#endif
    lv_obj_set_style_text_font(s.scr, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(s.scr, C(UI_C_TEXT), 0);

    build_topbar(s.scr);
    build_list(s.scr);
    build_dock(s.scr);

    chip_update(NULL);
    tempo_show(UI_TEMPO_DEFAULT);
    transpose_show(UI_TRANSPOSE_DEFAULT);
    play_button_update();

    lv_screen_load(s.scr);
    if (old && old != s.scr) {
        lv_obj_delete(old);
    }
    UI_UNLOCK();
}

void ui_set_ble_status(ui_ble_state_t state, const char *name)
{
    if (!UI_LOCK()) {
        return;
    }
    // A BLE task may report the same state repeatedly; ignore repeats so the
    // status pulse does not restart (visible hiccup) on every call.
    bool same = state == s.ble &&
                (state != UI_BLE_CONNECTED || strcmp(name ? name : "", lv_label_get_text(s.chip_name)) == 0);
    if (!same) {
        s.ble = state;
        chip_update(name);
        play_button_update();
    }
    UI_UNLOCK();
}

void ui_set_songs(const char *const *names, int count)
{
    ui_set_songs_pinned(names, count, 0);
}

void ui_set_songs_pinned(const char *const *names, int count, int pinned)
{
    if (!UI_LOCK()) {
        return;
    }
    // Remember the selected title so a list refresh does not move the selection
    char keep[96] = "";
    if (row_valid(s.selected)) {
        snprintf(keep, sizeof(keep), "%s", lv_label_get_text(s.rows[s.selected].name));
    }

    playing_apply(-1);   // also parks the EQ outside the list
    lv_obj_clean(s.list);
    lv_free(s.rows);
    s.rows = NULL;
    s.count = 0;
    s.selected = -1;

    if (count < 0 || !names) {
        count = 0;
    }
    if (count > 0) {
        s.rows = lv_malloc_zeroed(sizeof(ui_row_t) * (size_t)count);
        if (!s.rows) {
            count = 0;
        }
    }

    int sel = count > 0 ? 0 : -1;
    for (int i = 0; i < count; i++) {
        ui_row_t *r = &s.rows[i];
        r->row = box(s.list);
        lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_style(r->row, &st_row, 0);
        lv_obj_add_style(r->row, &st_row_sel, LV_STATE_CHECKED);
        lv_obj_add_style(r->row, &st_row_pressed, LV_STATE_PRESSED);
        lv_obj_add_style(r->row, &st_row_play, ROW_STATE_PLAYING);
        lv_obj_add_style(r->row, &st_row_sel_play, LV_STATE_CHECKED | ROW_STATE_PLAYING);
        lv_obj_add_style(r->row, &st_row_bad, ROW_STATE_BAD);

        if (i < pinned) {
            // Pinned entries (the lo-fi generator) take a loop icon instead of a number
            r->idx = text(r->row, LV_SYMBOL_LOOP);
            lv_obj_add_style(r->idx, &st_idx, 0);
            lv_obj_set_style_text_color(r->idx, C(UI_C_ACCENT_TEXT), 0);
        } else {
            char num[8];
            snprintf(num, sizeof(num), "%02d", (i - pinned + 1) % 100);
            r->idx = text(r->row, num);
            lv_obj_add_style(r->idx, &st_idx, 0);
        }

        char shown[96];
        display_name(shown, sizeof(shown), names[i]);
        r->name = text(r->row, shown);
        lv_obj_add_style(r->name, &st_name, 0);
        lv_label_set_long_mode(r->name, LV_LABEL_LONG_MODE_DOTS);

        if (keep[0] && strcmp(shown, keep) == 0) {
            sel = i;
        }
    }
    s.count = count;

    if (count > 0) {
        lv_obj_add_flag(s.empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s.empty, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_scroll_to_y(s.list, 0, LV_ANIM_OFF);
    lv_obj_update_layout(s.list);
    select_row(sel);
    list_reveal(sel, LV_ANIM_OFF);
    UI_UNLOCK();
}

void ui_set_playing(int index)
{
    if (!UI_LOCK()) {
        return;
    }
    if (index != s.playing) {
        playing_apply(index);
    }
    UI_UNLOCK();
}

void ui_set_song_title(int index, const char *title)
{
    if (!UI_LOCK()) {
        return;
    }
    if (row_valid(index) && title) {
        char shown[96];
        display_name(shown, sizeof(shown), title);
        lv_label_set_text(s.rows[index].name, shown);
    }
    UI_UNLOCK();
}

void ui_set_song_unsupported(int index)
{
    if (!UI_LOCK()) {
        return;
    }
    if (row_valid(index)) {
        row_mark_bad(index);
    }
    UI_UNLOCK();
}

void ui_set_tempo(int percent)
{
    if (!UI_LOCK()) {
        return;
    }
    tempo_show(percent);
    UI_UNLOCK();
}

void ui_set_transpose(int semitones)
{
    if (!UI_LOCK()) {
        return;
    }
    transpose_show(semitones);
    UI_UNLOCK();
}
