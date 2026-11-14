// Copyright 2020 Espressif Systems (Shanghai) Co. Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __BOARD_ESP32_DEVKITC_H__
#define __BOARD_ESP32_DEVKITC_H__

/* Push button configuration: GPIO 0 is the BOOT button on standard ESP32 boards */
#define LIGHT_BUTTON_GPIO          0    /* BOOT button on standard ESP32 */
#define LIGHT_BUTTON_ACTIVE_LEVEL  0

/* Hardware configuration for WS2812B NeoPixel 8-LED strip */
#define LIGHT_WS2818_GPIO          18    /* DIN pin for WS2812B NeoPixel strip */
#define LIGHT_WS2818_NUM_LEDS      8    /* 8 addressable RGB LEDs */

/* Hardware configuration for LEDC PWM on ESP32 */
#define LIGHT_GPIO_RED             25
#define LIGHT_GPIO_GREEN           26
#define LIGHT_GPIO_BLUE            27
#define LIGHT_GPIO_COLD            14
#define LIGHT_GPIO_WARM            12
#define LIGHT_FADE_PERIOD_MS       100  /**< The time from the current state to the next state */
#define LIGHT_BLINK_PERIOD_MS      1500 /**< Period of blinking lights */
#define LIGHT_FREQ_HZ              5000 /**< Frequency of ledc signal */

#endif /**< __BOARD_ESP32_DEVKITC_H__ */

