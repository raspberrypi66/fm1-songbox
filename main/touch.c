#include "touch.h"

#include "board.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"

static const char *TAG = "touch";

// 0 = LVGL polls the controller every refresh period (default, robust).
// 1 = use the INT pin. esp_lvgl_port then switches the input device to
//     event-only mode, which relies on the FT6336G pulsing INT for every
//     report including the release. The ft5x06 driver does not set the
//     controller's interrupt mode, so only enable this after checking on
//     hardware that release and press-and-hold (stepper repeat) still work.
#define TOUCH_USE_INT_PIN 0

#if TOUCH_DEBUG
#include "esp_log.h"
#include "esp_timer.h"

// Runs before the driver applies mirror/swap, so x/y are still raw
static void log_press(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                      uint8_t *point_num, uint8_t max_point_num)
{
    static int64_t s_last_us;
    int64_t now = esp_timer_get_time();
    bool new_press = now - s_last_us > 150000;   // LVGL polls every ~16 ms while touched
    s_last_us = now;
    if (!new_press || *point_num == 0) {
        return;
    }
    int sx = x[0], sy = y[0];
    if (BOARD_TOUCH_MIRROR_X) {
        sx = BOARD_LCD_V_RES - sx;
    }
    if (BOARD_TOUCH_MIRROR_Y) {
        sy = BOARD_LCD_H_RES - sy;
    }
    if (BOARD_TOUCH_SWAP_XY) {
        int t = sx;
        sx = sy;
        sy = t;
    }
    ESP_LOGI(TAG, "press raw x=%u y=%u -> screen x=%d y=%d", x[0], y[0], sx, sy);
}
#endif

esp_err_t touch_init(i2c_master_bus_handle_t bus, esp_lcd_touch_handle_t *out)
{
    ESP_RETURN_ON_FALSE(bus && out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.dev_addr = BOARD_TOUCH_ADDR;
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "panel io");

    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_V_RES,   // raw axes are portrait: x spans the 240 px side
        .y_max = BOARD_LCD_H_RES,
        .rst_gpio_num = BOARD_TOUCH_RST,
        .int_gpio_num = TOUCH_USE_INT_PIN ? BOARD_TOUCH_INT : GPIO_NUM_NC,
        .levels = {
            .reset = 0,       // active low
            .interrupt = 0,   // active low
        },
        .flags = {
            .swap_xy = BOARD_TOUCH_SWAP_XY,
            .mirror_x = BOARD_TOUCH_MIRROR_X,
            .mirror_y = BOARD_TOUCH_MIRROR_Y,
        },
#if TOUCH_DEBUG
        .process_coordinates = log_press,
#endif
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io, &cfg, out), TAG, "ft6336g");
    return ESP_OK;
}
