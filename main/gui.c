#include "gui.h"

#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "touch.h"

static const char *TAG = "gui";

esp_err_t gui_start(const display_t *disp, lv_display_t **out_disp)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // Default is 7 KB. The UI uses shadows, gradients and label marquees;
    // 8 KB leaves headroom. Check the high-water mark on hardware if changed.
    port_cfg.task_stack = 8192;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = disp->io,
        .panel_handle = disp->panel,
        .buffer_size = BOARD_LCD_H_RES * DISPLAY_DRAW_LINES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            // The port re-applies these to the panel, so they must match display_init()
            .swap_xy = DISPLAY_SWAP_XY,
            .mirror_x = DISPLAY_MIRROR_X,
            .mirror_y = DISPLAY_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,     // internal DMA-capable RAM
            .swap_bytes = true,   // the panel takes RGB565 big-endian over SPI
        },
    };
    lv_display_t *lv_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(lv_disp, ESP_FAIL, TAG, "add display");

    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c bus");
    esp_lcd_touch_handle_t tp = NULL;
    esp_err_t err = touch_init(i2c_bus_get(), &tp);
    if (err == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_disp,
            .handle = tp,
        };
        lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
        if (!indev) {
            ESP_LOGE(TAG, "add touch input failed");
        }
#if TOUCH_DEBUG
        // Red dot where LVGL sees the finger: shows at a glance if touch is
        // mirrored, rotated or offset against the picture
        if (indev && lvgl_port_lock(0)) {
            lv_obj_t *dot = lv_obj_create(lv_layer_sys());
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 16, 16);
            lv_obj_set_style_translate_x(dot, -8, 0);   // centre the dot on the point
            lv_obj_set_style_translate_y(dot, -8, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF2020), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(dot, lv_color_white(), 0);
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_indev_set_cursor(indev, dot);
            lvgl_port_unlock();
        }
#endif
    } else {
        // Keep the screen running without touch so the fault is visible, not a boot loop
        ESP_LOGE(TAG, "touch init failed (%s), continuing without touch", esp_err_to_name(err));
    }

    *out_disp = lv_disp;
    return ESP_OK;
}

void gui_reveal(void)
{
    if (lvgl_port_lock(0)) {
        lv_refr_now(NULL);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(30));   // let the last DMA transfer land
    display_set_backlight(true);
}
