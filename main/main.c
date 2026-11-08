#include "board.h"
#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// 1 = bring-up step 1: full-screen RGB colour test without LVGL, to check that
//     colours are not swapped and the orientation is right.
// 0 = normal: LVGL + touch + UI + app.
#define APP_RGB_COLOR_TEST 0

#if APP_RGB_COLOR_TEST
// RGB565
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_BLUE  0x001F
#else
#include "app.h"
#include "gui.h"
#endif

static const char *TAG = "main";

static void amp_off(void)
{
    // Speaker amp is not used in this project; its enable is active low
    gpio_set_level(BOARD_AMP_EN, 1);
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_AMP_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

#if APP_RGB_COLOR_TEST
// Bring-up step 1: full-screen red / green / blue, then an orientation frame
static void color_test(const display_t *disp)
{
    static const struct {
        uint16_t color;
        const char *name;
    } fills[] = {
        { C_RED, "RED" },
        { C_GREEN, "GREEN" },
        { C_BLUE, "BLUE" },
    };
    const int w = BOARD_LCD_H_RES;
    const int h = BOARD_LCD_V_RES;
    const int sq = 40;

    for (;;) {
        for (int i = 0; i < sizeof(fills) / sizeof(fills[0]); i++) {
            ESP_LOGI(TAG, "screen should be %s", fills[i].name);
            ESP_ERROR_CHECK(display_fill_rect(disp, 0, 0, w, h, fills[i].color));
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        ESP_LOGI(TAG, "orientation: black, white 1px border, RED top-left, GREEN top-right, BLUE bottom-left");
        ESP_ERROR_CHECK(display_fill_rect(disp, 0, 0, w, h, C_BLACK));
        ESP_ERROR_CHECK(display_fill_rect(disp, 0, 0, w, 1, C_WHITE));
        ESP_ERROR_CHECK(display_fill_rect(disp, 0, h - 1, w, 1, C_WHITE));
        ESP_ERROR_CHECK(display_fill_rect(disp, 0, 0, 1, h, C_WHITE));
        ESP_ERROR_CHECK(display_fill_rect(disp, w - 1, 0, 1, h, C_WHITE));
        ESP_ERROR_CHECK(display_fill_rect(disp, 4, 4, sq, sq, C_RED));
        ESP_ERROR_CHECK(display_fill_rect(disp, w - 4 - sq, 4, sq, sq, C_GREEN));
        ESP_ERROR_CHECK(display_fill_rect(disp, 4, h - 4 - sq, sq, sq, C_BLUE));
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}
#endif

// NVS holds the BLE bond keys and the last connected FM-1 address
static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    amp_off();
    nvs_init();

    static display_t disp;   // static: the LVGL port keeps using the handles
    ESP_ERROR_CHECK(display_init(&disp));

#if APP_RGB_COLOR_TEST
    ESP_ERROR_CHECK(display_fill_rect(&disp, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, C_BLACK));
    display_set_backlight(true);
    color_test(&disp);
#else
    lv_display_t *lv_disp = NULL;
    ESP_ERROR_CHECK(gui_start(&disp, &lv_disp));
    app_start(lv_disp);
    gui_reveal();
    ESP_LOGI(TAG, "UI running");
#endif
}
