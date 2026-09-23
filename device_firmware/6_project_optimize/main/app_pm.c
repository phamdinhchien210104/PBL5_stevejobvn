/*
 * ESP32-S3 & ESP32-C3 Smart Light Project
 * Chapter 12: Power Management in Smart Light Project
 *
 * Implements:
 * 1. Dynamic Frequency Scaling (DFS 40 - 160 MHz)
 * 2. Automatic Light-sleep with FreeRTOS Tickless Idle
 * 3. Power Management Lock (ESP_PM_NO_LIGHT_SLEEP) to ensure stable WS2812B LED clocking when ON
 * 4. Automatic Light-sleep entry when OFF, lowering standby current to < 2-5 mA
 * 5. GPIO wakeup for instantaneous response on physical button press
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

#include DEVELOPMENT_BOARD
#include "app_priv.h"

static const char *TAG = "app_pm";

#if CONFIG_PM_ENABLE

#define LIGHT_EXAMPLE_MAX_CPU_FREQ_MHZ (160)
#define LIGHT_EXAMPLE_MIN_CPU_FREQ_MHZ (40)

static bool g_pm_lock_acquired = false;
static esp_pm_lock_handle_t s_light_pm_lock = NULL;

esp_err_t app_pm_init(void)
{
    ESP_LOGI(TAG, "Initializing Power Management (DFS: %d - %d MHz, Automatic Light-sleep: %s)...",
             LIGHT_EXAMPLE_MIN_CPU_FREQ_MHZ,
             LIGHT_EXAMPLE_MAX_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
             "ENABLED"
#else
             "DISABLED"
#endif
    );

    // 1. Configure DFS and Light-sleep using unified esp_pm_config_t
    esp_pm_config_t pm_config = {
        .max_freq_mhz = LIGHT_EXAMPLE_MAX_CPU_FREQ_MHZ,
        .min_freq_mhz = LIGHT_EXAMPLE_MIN_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#else
        .light_sleep_enable = false
#endif
    };
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Dynamic Frequency Scaling configured successfully (40MHz idle <-> 160MHz active)");

    // 2. Create Power Management lock preventing light sleep when LED is active
    if (s_light_pm_lock == NULL) {
        err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "light_led_lock", &s_light_pm_lock);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create PM lock 'light_led_lock': %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "PM Lock 'light_led_lock' (ESP_PM_NO_LIGHT_SLEEP) created successfully");
    }

    // 3. Configure GPIO wakeup for physical Boot button
    err = gpio_wakeup_enable(LIGHT_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) {
        err = esp_sleep_enable_gpio_wakeup();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "GPIO %d configured as Light-sleep wakeup source", LIGHT_BUTTON_GPIO);
        } else {
            ESP_LOGW(TAG, "Failed to enable GPIO sleep wakeup: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "Failed to enable GPIO %d wakeup: %s", LIGHT_BUTTON_GPIO, esp_err_to_name(err));
    }

    return ESP_OK;
}

esp_err_t app_pm_lock_acquire(void)
{
    if (!g_pm_lock_acquired && s_light_pm_lock != NULL) {
        esp_err_t err = esp_pm_lock_acquire(s_light_pm_lock);
        if (err == ESP_OK) {
            g_pm_lock_acquired = true;
            ESP_LOGI(TAG, "==> [PM LOCK ACQUIRED]: Light ON -> Light-sleep PROHIBITED, Peripheral/LED clocks guaranteed");
        } else {
            ESP_LOGE(TAG, "Failed to acquire PM lock: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        ESP_LOGD(TAG, "PM lock already held");
    }
    return ESP_OK;
}

esp_err_t app_pm_lock_release(void)
{
    if (g_pm_lock_acquired && s_light_pm_lock != NULL) {
        esp_err_t err = esp_pm_lock_release(s_light_pm_lock);
        if (err == ESP_OK) {
            g_pm_lock_acquired = false;
            ESP_LOGI(TAG, "==> [PM LOCK RELEASED]: Light OFF -> Light-sleep PERMITTED (Standby target < 2-5mA)");
        } else {
            ESP_LOGE(TAG, "Failed to release PM lock: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        ESP_LOGD(TAG, "PM lock already released");
    }
    return ESP_OK;
}

#else

esp_err_t app_pm_init(void)
{
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE is not set in sdkconfig; Power Management disabled");
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
