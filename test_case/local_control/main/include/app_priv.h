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
 * @brief Wi-Fi Connection Status for Visual LED Indication
 */
typedef enum {
    WIFI_STATUS_CONNECTING, /**< Connecting to AP (Breathing Amber/Yellow) */
    WIFI_STATUS_CONNECTED,  /**< Connected to AP with IP (Solid Green) */
    WIFI_STATUS_FAILED,     /**< Connection failed after retries (Solid Red) */
} wifi_status_t;

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
 * @brief Indicate Wi-Fi status via WS2812B NeoPixel 8-bit LED strip
 * 
 * @param status Status enum (CONNECTING, CONNECTED, FAILED)
 */
void app_driver_set_wifi_status(wifi_status_t status);

/**
 * @brief Flash brief visual pulse on LEDs to acknowledge incoming control command
 */
void app_driver_pulse_feedback(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /**< __APP_PRIVATE_H__ */
