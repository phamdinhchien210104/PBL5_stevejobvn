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

#ifndef __APP_PRIVATE_H__
#define __APP_PRIVATE_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize application driver (WS2812B LED strip & Boot button)
 */
void app_driver_init(void);

/**
 * @brief Set on/off state of light
 * 
 * @param state true = ON, false = OFF
 * @return int ESP_OK on success
 */
int app_driver_set_state(bool state);

/**
 * @brief Get current light switch state
 * 
 * @return true if ON, false if OFF
 */
bool app_driver_get_state(void);

/**
 * @brief Toggle light switch state
 * 
 * @return int ESP_OK on success
 */
int app_driver_toggle_state(void);

/**
 * @brief Set RGB color of light
 * 
 * @param red Red value (0-255)
 * @param green Green value (0-255)
 * @param blue Blue value (0-255)
 * @return int ESP_OK on success
 */
int app_driver_set_color(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Get current brightness level (5 - 100%)
 * 
 * @return uint8_t brightness percentage
 */
uint8_t app_driver_get_brightness(void);

/**
 * @brief Set brightness level (5 - 100%)
 * 
 * @param brightness Target brightness percentage
 * @return int ESP_OK on success
 */
int app_driver_set_brightness(uint8_t brightness);

/**
 * @brief Increment or decrement brightness level by delta percentage
 * 
 * @param delta Positive to increase, negative to decrease (e.g. +20, -20)
 * @return int ESP_OK on success
 */
int app_driver_adjust_brightness(int delta);

/**
 * @brief Get current color index in 8-color preset table (0 - 7)
 * 
 * @return uint8_t color index
 */
uint8_t app_driver_get_color_index(void);

/**
 * @brief Get human-readable name of current color preset
 * 
 * @return const char* color name string
 */
const char* app_driver_get_color_name(void);

/**
 * @brief Get current RGB components of color preset
 * 
 * @param r Red output pointer
 * @param g Green output pointer
 * @param b Blue output pointer
 */
void app_driver_get_rgb(uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * @brief Switch to next color in 8-color preset table (equivalent to Boot double-click)
 * 
 * @return int ESP_OK on success
 */
int app_driver_next_color(void);

#ifdef __cplusplus
}
#endif

#endif /**< __APP_PRIVATE_H__ */
