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

#ifndef __APP_PRIVATE_H__
#define __APP_PRIVATE_H__

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Wi-Fi Provisioning Status for Visual LED Indication
 */
typedef enum {
    PROV_STATUS_WAITING,    /**< Waiting for smartphone to connect via BLE (Breathing Cyan) */
    PROV_STATUS_CONNECTING, /**< Received credentials from phone, connecting to AP (Breathing Yellow) */
    PROV_STATUS_SUCCESS,    /**< Provisioning & IP connection successful (Solid Green) */
    PROV_STATUS_FAILED,     /**< Provisioning credentials failed (Solid Red) */
} prov_status_t;

/**
 * @brief Initialize application driver (WS2812B LED strip & button)
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
 * @brief Indicate Provisioning status via WS2812B NeoPixel 8-bit LED strip
 * 
 * @param status Status enum (WAITING, CONNECTING, SUCCESS, FAILED)
 */
void app_driver_set_prov_status(prov_status_t status);

#endif /**< __APP_PRIVATE_H__ */
