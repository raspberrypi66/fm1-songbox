#pragma once

#include "display.h"
#include "esp_err.h"
#include "lvgl.h"

// Start LVGL through esp_lvgl_port on the already initialised panel, and add
// the FT6336G touch as its input device (I2C bus created if needed).
esp_err_t gui_start(const display_t *disp, lv_display_t **out_disp);

// Render the first frame, then switch the backlight on, so the panel never
// shows uninitialised memory.
void gui_reveal(void);
