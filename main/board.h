#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

// LCDWiki ES3C28P pin map. Every pin here was checked against the vendor
// schematic. Do not add pins that are not there.

// LCD: ILI9341 on SPI, landscape 320x240
#define BOARD_LCD_SPI_HOST   SPI2_HOST
#define BOARD_LCD_CS         GPIO_NUM_10
#define BOARD_LCD_SCLK       GPIO_NUM_12
#define BOARD_LCD_MOSI       GPIO_NUM_11
#define BOARD_LCD_MISO       GPIO_NUM_13
#define BOARD_LCD_DC         GPIO_NUM_46   // strapping pin, fine as output after boot
#define BOARD_LCD_RST        GPIO_NUM_NC   // tied to EN
#define BOARD_LCD_BL         GPIO_NUM_45   // high = on
#define BOARD_LCD_PCLK_HZ    (40 * 1000 * 1000)   // drop to 20 MHz if artifacts
#define BOARD_LCD_H_RES      320
#define BOARD_LCD_V_RES      240

// Shared I2C bus (touch 0x38, ES8311 0x18 left alone)
#define BOARD_I2C_SDA        GPIO_NUM_16
#define BOARD_I2C_SCL        GPIO_NUM_15

// Touch FT6336G
#define BOARD_TOUCH_ADDR     0x38
#define BOARD_TOUCH_RST      GPIO_NUM_18   // active low
#define BOARD_TOUCH_INT      GPIO_NUM_17   // active low

// Touch orientation for landscape. Raw touch coordinates are portrait
// (x 0..239, y 0..319). esp_lcd_touch applies the mirrors to the RAW axes
// before the swap, so:
//   taps land mirrored left/right on screen -> toggle BOARD_TOUCH_MIRROR_Y
//   taps land mirrored top/bottom on screen -> toggle BOARD_TOUCH_MIRROR_X
// MIRROR_X = 1: swap alone came out mirrored top/bottom on the board.
// Confirmed on hardware 2026-10-26 (TOUCH_DEBUG dot follows the finger).
#define BOARD_TOUCH_SWAP_XY   1
#define BOARD_TOUCH_MIRROR_X  1
#define BOARD_TOUCH_MIRROR_Y  0

#define BOARD_BOOT_BTN       GPIO_NUM_0

// Speaker amp enable, active low. Not used: keep high = amp off
#define BOARD_AMP_EN         GPIO_NUM_1
