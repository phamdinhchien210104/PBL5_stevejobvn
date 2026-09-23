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

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define DEFAULT_POWER       true
#define DEFAULT_HUE         180
#define DEFAULT_SATURATION  100
#define DEFAULT_BRIGHTNESS  25

/**
 * @brief Initialize application driver (push button and WS2812B NeoPixel SPI2 DMA)
 */
void app_driver_init(void);

/**
 * @brief Set output state directly (IRAM safe)
 *
 * @param state Target state (true: ON, false: OFF)
 * @return int ESP_OK on success
 */
int app_driver_set_state(bool state);

/**
 * @brief Get current output state
 *
 * @return true if light is ON, false if OFF
 */
bool app_driver_get_state(void);

/**
 * @brief Set light power state with Power Management lock integration
 *
 * @param power true to turn ON (acquires PM lock), false to turn OFF (releases PM lock)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_light_set_power(bool power);

/**
 * @brief Set light color in HSV model
 *
 * @param hue Hue angle (0 - 360)
 * @param saturation Saturation percentage (0 - 100)
 * @param brightness Brightness percentage (0 - 100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_light_set(uint32_t hue, uint32_t saturation, uint32_t brightness);

/**
 * @brief Set light brightness
 *
 * @param brightness Brightness percentage (0 - 100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_light_set_brightness(uint16_t brightness);

/**
 * @brief Set light hue
 *
 * @param hue Hue angle (0 - 360)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_light_set_hue(uint16_t hue);

/**
 * @brief Set light saturation
 *
 * @param saturation Saturation percentage (0 - 100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_light_set_saturation(uint16_t saturation);

/**
 * @brief Initialize Power Management (DFS 40-160MHz, Automatic Light-sleep, PM lock)
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_pm_init(void);

/**
 * @brief Acquire PM Lock preventing Light-sleep while LED pulse is active
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_pm_lock_acquire(void);

/**
 * @brief Release PM Lock allowing chip to enter Automatic Light-sleep
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_pm_lock_release(void);

#endif /**< __APP_PRIVATE_H__ */
