/*
 * ESP32-C3 SuperMini / DevKitC Hardware Configuration
 * PBL5 Smart Light Architecture
 */

#ifndef __BOARD_ESP32C3_DEVKITC_H__
#define __BOARD_ESP32C3_DEVKITC_H__

/* Nút Boot vật lý trên ESP32-C3 DevKit / SuperMini */
#define LIGHT_BUTTON_GPIO          9
#define LIGHT_BUTTON_ACTIVE_LEVEL  0

/* Chân dữ liệu thanh LED WS2812B (8 hạt NeoPixel) điều khiển qua Hardware SPI2 DMA */
#define LIGHT_WS2818_GPIO          4
#define LIGHT_WS2818_NUM_LEDS      8
#define LIGHT_GPIO_WS2812          LIGHT_WS2818_GPIO
#define LIGHT_WS2812_NUM_LEDS      LIGHT_WS2818_NUM_LEDS

#endif /* __BOARD_ESP32C3_DEVKITC_H__ */
