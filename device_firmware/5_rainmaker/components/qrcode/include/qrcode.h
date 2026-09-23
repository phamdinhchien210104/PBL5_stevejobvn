#pragma once

#include "esp_err.h"
#include "../../managed_components/espressif__qrcode/include/qrcode.h"

/**
 * @brief Legacy RainMaker qrcode_display shim mapping to modern esp_qrcode_generate
 */
static inline esp_err_t qrcode_display(const char *text)
{
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    return esp_qrcode_generate(&cfg, text);
}
