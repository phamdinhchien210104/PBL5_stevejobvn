/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_pm.h"

#include "app_priv.h"

static const char *TAG = "app_pm";

#if CONFIG_PM_ENABLE

#if CONFIG_IDF_TARGET_ESP32C3
#define LIGHT_EXAMPLE_MAX_CPU_FREQ_MHZ (160)
#define LIGHT_EXAMPLE_MIN_CPU_FREQ_MHZ (40)
#else
#define LIGHT_EXAMPLE_MAX_CPU_FREQ_MHZ (80)
#define LIGHT_EXAMPLE_MIN_CPU_FREQ_MHZ (10)
#endif

static bool g_pm_lock_acquired = false;
static esp_pm_lock_handle_t g_pm_apb_lock = NULL;

esp_err_t app_pm_init(void)
{
    // Configure dynamic frequency scaling:
    // maximum and minimum frequencies are set in sdkconfig,
    // automatic light sleep is enabled if tickless idle support is enabled.
    esp_pm_config_t pm_config = {
        .max_freq_mhz = LIGHT_EXAMPLE_MAX_CPU_FREQ_MHZ,
        .min_freq_mhz = LIGHT_EXAMPLE_MIN_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true,
#else
        .light_sleep_enable = false,
#endif
    };

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power management: %s", esp_err_to_name(ret));
        return ret;
    }

    if (g_pm_apb_lock == NULL) {
        ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "l_apb", &g_pm_apb_lock);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create esp_pm lock 'l_apb': %s", esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGI(TAG, "Power Management initialized successfully (Max: %d MHz, Min: %d MHz)",
             LIGHT_EXAMPLE_MAX_CPU_FREQ_MHZ, LIGHT_EXAMPLE_MIN_CPU_FREQ_MHZ);
    return ESP_OK;
}

esp_err_t app_pm_lock_acquire(void)
{
    if (!g_pm_apb_lock) {
        ESP_LOGW(TAG, "PM lock not initialized yet");
        return ESP_OK;
    }

    if (!g_pm_lock_acquired) {
        esp_err_t ret = esp_pm_lock_acquire(g_pm_apb_lock);
        if (ret == ESP_OK) {
            g_pm_lock_acquired = true;
            ESP_LOGD(TAG, "APB PM lock acquired");
        } else {
            ESP_LOGE(TAG, "Failed to acquire APB PM lock: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGD(TAG, "APB PM lock already acquired");
    }
    return ESP_OK;
}

esp_err_t app_pm_lock_release(void)
{
    if (!g_pm_apb_lock) {
        return ESP_OK;
    }

    if (g_pm_lock_acquired) {
        esp_err_t ret = esp_pm_lock_release(g_pm_apb_lock);
        if (ret == ESP_OK) {
            g_pm_lock_acquired = false;
            ESP_LOGD(TAG, "APB PM lock released");
        } else {
            ESP_LOGE(TAG, "Failed to release APB PM lock: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGD(TAG, "APB PM lock already released");
    }

    return ESP_OK;
}

#else

esp_err_t app_pm_init(void)
{
    ESP_LOGI(TAG, "Power Management is disabled in sdkconfig");
    return ESP_OK;
}

esp_err_t app_pm_lock_acquire(void)
{
    return ESP_OK;
}

esp_err_t app_pm_lock_release(void)
{
    return ESP_OK;
}

#endif // CONFIG_PM_ENABLE
