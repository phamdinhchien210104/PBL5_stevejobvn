// Copyright 2026 PBL5 Stevejobvn Team
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

#ifndef __BOARD_ESP32S3_DEVKITC_H__
#define __BOARD_ESP32S3_DEVKITC_H__

/* Hardware configuration for ESP32-S3 DevKit */
#define LIGHT_BUTTON_GPIO          0    /* Boot button on ESP32-S3 */
#define LIGHT_BUTTON_ACTIVE_LEVEL  0

#define LIGHT_WS2818_GPIO          4    /* DIN pin for WS2812B NeoPixel 8-LED strip */
#define LIGHT_WS2818_NUM_LEDS      8    /* 8 addressable RGB LEDs */

#endif /**< __BOARD_ESP32S3_DEVKITC_H__ */
