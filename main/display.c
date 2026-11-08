#include "display.h"

#include "board.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "display";

#define FILL_LINES DISPLAY_DRAW_LINES

static SemaphoreHandle_t s_trans_done;
static uint16_t *s_fill_buf;

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &woken);
    return woken == pdTRUE;
}

void display_set_backlight(bool on)
{
    gpio_set_level(BOARD_LCD_BL, on ? 1 : 0);
}

esp_err_t display_init(display_t *disp)
{
    // Backlight off until the panel shows valid content
    gpio_set_level(BOARD_LCD_BL, 0);
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << BOARD_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg), TAG, "backlight gpio");

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = BOARD_LCD_SCLK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H_RES * FILL_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = BOARD_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_cfg, &disp->io),
                        TAG, "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_RST,   // NC: driver sends a software reset
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(disp->io, &panel_cfg, &disp->panel), TAG, "ili9341");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(disp->panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(disp->panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(disp->panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(disp->panel, DISPLAY_SWAP_XY), TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(disp->panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(disp->panel, true), TAG, "display on");

    ESP_LOGI(TAG, "ILI9341 %dx%d ready, pclk %d MHz", BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_LCD_PCLK_HZ / 1000000);
    return ESP_OK;
}

esp_err_t display_fill_rect(const display_t *disp, int x, int y, int w, int h, uint16_t rgb565)
{
    ESP_RETURN_ON_FALSE(w > 0 && h > 0 && w <= BOARD_LCD_H_RES, ESP_ERR_INVALID_ARG, TAG, "bad rect");

    if (!s_fill_buf) {
        // First use: take the transfer-done callback (esp_lvgl_port would
        // register its own instead, which is why LVGL and this helper never mix)
        s_trans_done = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_trans_done, ESP_ERR_NO_MEM, TAG, "semaphore");
        const esp_lcd_panel_io_callbacks_t cbs = {
            .on_color_trans_done = on_color_trans_done,
        };
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(disp->io, &cbs, NULL), TAG, "callbacks");
        s_fill_buf = heap_caps_malloc(BOARD_LCD_H_RES * FILL_LINES * sizeof(uint16_t),
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_RETURN_ON_FALSE(s_fill_buf, ESP_ERR_NO_MEM, TAG, "fill buffer");
    }

    // The panel takes RGB565 big-endian over SPI
    uint16_t px = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
    int lines = (BOARD_LCD_H_RES * FILL_LINES) / w;
    if (lines > h) {
        lines = h;
    }
    for (int i = 0; i < w * lines; i++) {
        s_fill_buf[i] = px;
    }

    for (int row = y; row < y + h; row += lines) {
        int n = (y + h - row < lines) ? (y + h - row) : lines;
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(disp->panel, x, row, x + w, row + n, s_fill_buf), TAG, "draw");
        xSemaphoreTake(s_trans_done, portMAX_DELAY);
    }
    return ESP_OK;
}
