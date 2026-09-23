#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and enable ESP Insights using shared RainMaker MQTT transport
 *
 * @return ESP_OK on success, appropriate esp_err_t otherwise
 */
esp_err_t app_insights_enable(void);

#ifdef __cplusplus
}
#endif
