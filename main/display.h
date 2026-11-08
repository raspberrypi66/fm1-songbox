#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

// Panel orientation for landscape 320x240. Used by display_init() and handed
// to esp_lvgl_port, which re-applies it when it registers the display.
#define DISPLAY_SWAP_XY   true
#define DISPLAY_MIRROR_X  false
#define DISPLAY_MIRROR_Y  false

// Lines per draw buffer (LVGL: two buffers of 320 x 40 px in internal DMA RAM).
// Also sizes the SPI max transfer.
#define DISPLAY_DRAW_LINES 40

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
} display_t;

// SPI bus + ILI9341 panel in landscape. Backlight stays off until display_set_backlight(true).
// Registers no transfer-done callback: whoever draws (LVGL port or the fill test) owns it.
esp_err_t display_init(display_t *disp);

void display_set_backlight(bool on);

// Fill a rectangle with one RGB565 color. Blocks until the transfer is done.
// Bring-up helper only. It takes over the panel IO's transfer-done callback on
// first use, so never call it once LVGL (esp_lvgl_port) is running.
esp_err_t display_fill_rect(const display_t *disp, int x, int y, int w, int h, uint16_t rgb565);
