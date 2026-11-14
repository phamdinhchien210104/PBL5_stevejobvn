/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "iot_button.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_params.h"
#include "esp_rmaker_utils.h"

#include "light_driver.h"
#include DEVELOPMENT_BOARD
#include "app_priv.h"

static const char *TAG = "app_driver";

#define REBOOT_DELAY        2

static bool g_output_state = DEFAULT_POWER;

extern esp_rmaker_device_t *light_device;

static void push_btn_cb(void *arg)
{
    bool new_state = !g_output_state;
    ESP_LOGW(TAG, "==> [Physical Button Click]: Toggling light to %s", new_state ? "ON" : "OFF");
    app_light_set_power(new_state);

    if (light_device) {
        esp_rmaker_param_t *power_param = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_POWER_NAME);
        if (power_param) {
            esp_rmaker_param_update_and_report(power_param, esp_rmaker_bool(new_state));
        }
    }
}

static void factory_reset_trigger(void *arg)
{
    ESP_LOGW(TAG, "==> [Physical Button Long Press]: Resetting to factory defaults in %d seconds...", REBOOT_DELAY);
    esp_rmaker_factory_reset(0, REBOOT_DELAY);
}

void app_driver_init(void)
{
    /* 1. Configure push button using iot_button */
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num     = LIGHT_BUTTON_GPIO,
            .active_level = LIGHT_BUTTON_ACTIVE_LEVEL,
        },
    };
    button_handle_t btn_handle = iot_button_create(&btn_cfg);
    if (btn_handle) {
        /* Register a callback for a button short press event */
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, push_btn_cb);
        /* Register a callback for a button long press event */
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, factory_reset_trigger);
        ESP_LOGI(TAG, "Button registered on GPIO %d (Click: Toggle, Long Press: Reset)", LIGHT_BUTTON_GPIO);
    } else {
        ESP_LOGE(TAG, "Failed to create iot_button handle on GPIO %d", LIGHT_BUTTON_GPIO);
    }

    /**
     * 2. Light driver initialization (supports LEDC and WS2812 if defined)
     */
    light_driver_config_t driver_config = {
#ifdef LIGHT_WS2818_GPIO
        .gpio_ws2812     = LIGHT_WS2818_GPIO,
        .num_leds        = LIGHT_WS2818_NUM_LEDS,
#endif
        .gpio_red        = LIGHT_GPIO_RED,
        .gpio_green      = LIGHT_GPIO_GREEN,
        .gpio_blue       = LIGHT_GPIO_BLUE,
        .gpio_cold       = LIGHT_GPIO_COLD,
        .gpio_warm       = LIGHT_GPIO_WARM,
        .fade_period_ms  = LIGHT_FADE_PERIOD_MS,
        .blink_period_ms = LIGHT_BLINK_PERIOD_MS,
        .freq_hz         = LIGHT_FREQ_HZ,
        .clk_cfg         = LEDC_USE_APB_CLK,
        .duty_resolution = LEDC_TIMER_11_BIT,
    };
    ESP_ERROR_CHECK(light_driver_init(&driver_config));

    /* 3. Set default state */
    app_light_set_power(DEFAULT_POWER);
}

int IRAM_ATTR app_driver_set_state(bool state)
{
    return app_light_set_power(state);
}

bool app_driver_get_state(void)
{
    return g_output_state;
}

esp_err_t app_light_set_power(bool power)
{
    g_output_state = power;
    if (power) {
        app_pm_lock_acquire();
        ESP_LOGI(TAG, "Light ON");
        light_driver_set_switch(true);
    } else {
        ESP_LOGI(TAG, "Light OFF");
        light_driver_set_switch(false);
        app_pm_lock_release();
    }
    return ESP_OK;
}

esp_err_t app_light_set(uint32_t hue, uint32_t saturation, uint32_t brightness)
{
    return light_driver_set_hsv(hue, saturation, brightness);
}

esp_err_t app_light_set_brightness(uint16_t brightness)
{
    return light_driver_set_value((uint8_t)brightness);
}

esp_err_t app_light_set_hue(uint16_t hue)
{
    return light_driver_set_hue(hue);
}

esp_err_t app_light_set_saturation(uint16_t saturation)
{
    return light_driver_set_saturation((uint8_t)saturation);
}
