#pragma once

// Design tokens for the FM-1 Song Box screen (320x240, ILI9341, RGB565).
// Every colour, size and spacing used by ui.c comes from here, so the look
// stays consistent and can be retuned in one place.
//
// Colours are written as RGB888 hex. The panel is RGB565, so the darkest
// values are chosen to stay distinct after quantisation (R/B step 8, G step 4).

#include "lvgl.h"

// 1 = bitmap skin (main/ui/assets/img_skin_*.c, drawn by tools/ui_assets):
//     background plate, glossy play orb, glass row card. Live widgets on top.
// 0 = flat theme drawn by LVGL only (no skin bitmaps). Same geometry.
// Flip to 0 to compare scrolling smoothness on hardware.
#ifndef UI_USE_SKIN
#define UI_USE_SKIN 1
#endif

// ---- Palette: cool graphite base, one violet accent, three status colours ----
// Elevation goes BG (deepest) -> CHROME (top bar, dock) -> SURFACE (cards,
// steppers) -> SURFACE_HI (pressed). Each step is a small lift in value, so
// layering reads without heavy borders.
#define UI_C_BG           0x0B0E14
#define UI_C_CHROME       0x12161F
#define UI_C_SURFACE      0x1B212D
#define UI_C_SURFACE_HI   0x2A3242
#define UI_C_HAIRLINE     0x1F2531   // 1 px separators
#define UI_C_BORDER       0x2B3344   // 1 px edges of cards and steppers

#define UI_C_TEXT         0xF2F4F8   // primary text, values
#define UI_C_TEXT_2       0xAAB3C2   // song names (resting), stepper glyphs
#define UI_C_TEXT_3       0x677186   // captions, track numbers, units
#define UI_C_TEXT_DIS     0x434B5B   // disabled glyphs
#define UI_C_ICON_DIS     (UI_USE_SKIN ? 0x5D667A : UI_C_TEXT_DIS)   // play glyph on the disabled orb
// Dock captions sit on brushed metal in the skin: muted grey drops to ~2:1
// there, so they use TEXT_2 (~5:1 measured on the plate)
#define UI_C_DOCK_CAPTION (UI_USE_SKIN ? UI_C_TEXT_2 : UI_C_TEXT_3)

// Accent: the only decorative colour. Means "active / the thing to press".
// Violet does not collide with any status colour (green/amber/red).
#define UI_C_ACCENT       0x7C5CFF
#define UI_C_ACCENT_TOP   0x9277FF   // play button gradient, top
#define UI_C_ACCENT_BOT   0x6040F0   // play button gradient, bottom
#define UI_C_ACCENT_PR_T  0x6D53E6   // pressed gradient, top
#define UI_C_ACCENT_PR_B  0x4E31CC   // pressed gradient, bottom
#define UI_C_ACCENT_TEXT  0xAD9BFF   // accent used as text on dark (contrast ~8:1)
#define UI_C_ACCENT_PALE  0xDCD4FF   // accent text on the violet "live" card (skin)

// Status colours: meaning only, never decoration
#define UI_C_OK           0x3DD68C   // connected
#define UI_C_WARN         0xFFB547   // scanning / connecting
#define UI_C_ERR          0xFF5D6E   // disconnected, unsupported file

// ---- Typography (LVGL built-in Montserrat, one weight) ----
// Hierarchy comes from size and colour, since only one weight is available.
#define UI_FONT_CAPTION   (&lv_font_montserrat_12)   // labels, chip, track no.
#define UI_FONT_BODY      (&lv_font_montserrat_16)   // song names
#define UI_FONT_VALUE     (&lv_font_montserrat_20)   // tempo / transpose values
#define UI_CAPTION_TRACK  1                           // letter spacing for caps captions

// ---- Spacing grid (4 px) and geometry ----
#define UI_SP_1           4
#define UI_SP_2           8
#define UI_SP_3           12
#define UI_EDGE           UI_SP_2      // outer margin on every side

#define UI_TOPBAR_H       28
#define UI_DOCK_H         68
#define UI_LIST_H         (240 - UI_TOPBAR_H - UI_DOCK_H)   // 144 = 4 rows
#define UI_ROW_H          36
#define UI_ROW_SLOT_W     32           // leading slot: track no. / EQ / warning

#define UI_RADIUS_CARD    10
#define UI_CHIP_H         20

#define UI_PLAY_D         52           // play/stop circle diameter
#define UI_STEP_W         116          // stepper pill: [-] value [+]
#define UI_STEP_H         38
#define UI_STEP_BTN_W     37           // >= 36 px touch target (plus 1 px ext click area)
#define UI_STEP_BTN_H     36
