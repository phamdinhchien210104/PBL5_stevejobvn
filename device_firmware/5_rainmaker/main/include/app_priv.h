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

#define DEFAULT_POWER       true
#define DEFAULT_HUE         180
#define DEFAULT_SATURATION  100
#define DEFAULT_BRIGHTNESS  25

/**
 * @brief Initialize application driver (WS2812B Hardware SPI2 DMA + Boot Button HAL)
 */
void app_driver_init(void);

/**
 * @brief Set hardware light switch state
 */
int app_driver_set_state(bool state);

/**
 * @brief Get current hardware light state
 */
bool app_driver_get_state(void);

/**
 * @brief Set light HSV composite
 */
esp_err_t app_light_set(uint32_t hue, uint32_t saturation, uint32_t brightness);

/**
 * @brief Set light power (ON/OFF)
 */
esp_err_t app_light_set_power(bool power);

/**
 * @brief Set light brightness (0-100)
 */
esp_err_t app_light_set_brightness(uint16_t brightness);

/**
 * @brief Set light hue (0-360)
 */
esp_err_t app_light_set_hue(uint16_t hue);

/**
 * @brief Set light saturation (0-100)
 */
esp_err_t app_light_set_saturation(uint16_t saturation);

#endif /**< __APP_PRIVATE_H__ */
