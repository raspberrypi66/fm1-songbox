#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"

// FT6336G capacitive touch (esp_lcd_touch_ft5x06 driver) on the shared I2C bus.
// Orientation flags live in board.h (BOARD_TOUCH_*).

// 1 = bring-up aid for touch orientation: log each new press (raw and screen
// coordinates) and show a red dot where LVGL sees the touch. 0 for normal use.
#define TOUCH_DEBUG 0
esp_err_t touch_init(i2c_master_bus_handle_t bus, esp_lcd_touch_handle_t *out);
