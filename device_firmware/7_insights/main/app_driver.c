/*
 * ESP32-S3 & ESP32-C3 Smart Light Project
 * Chapter 15: Using ESP Insights in Smart Light Project
 *
 * Driver Implementation:
 * 1. Physical Boot button with Multi-Gestures (Single click: Toggle, Double click: Hue cycle, 5x click: Test Crash, Long press: Factory reset)
 * 2. WS2812B NeoPixel 8-LED strip via Hardware SPI2 DMA @ 3.2MHz on GPIO 4
 * 3. Full HSV color model integration with Cloud Device Shadow sync
 * 4. Automatic Power Management lock coordination (Acquire when Light ON, Release when Light OFF)
 * 5. ESP Insights Custom Diagnostic Events (ESP_DIAG_EVENT) on light state/color transitions
 * 6. Section 15.3.3 Crash Simulation for Core Dump verification
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "iot_button.h"
#include "light_driver.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_utils.h>
#include <esp_diagnostics.h>
#include <esp_insights.h>

#include DEVELOPMENT_BOARD
#include "app_priv.h"

static const char *TAG = "app_driver";

#define REBOOT_DELAY 2

static bool g_output_state = DEFAULT_POWER;
static uint16_t g_hue = DEFAULT_HUE;
static uint8_t g_saturation = DEFAULT_SATURATION;
static uint8_t g_brightness = DEFAULT_BRIGHTNESS;

extern esp_rmaker_device_t *light_device;

/* Preset hues for double click cycling */
static const uint16_t s_preset_hues[] = {0, 60, 120, 180, 240, 300};
#define NUM_PRESET_HUES (sizeof(s_preset_hues) / sizeof(s_preset_hues[0]))
static size_t s_hue_index = 0;

void app_trigger_crash_test(void)
{
    ESP_LOGE(TAG, "==========================================================");
    ESP_LOGE(TAG, "  TRIGGERING ARTIFICIAL CRASH / PANIC (Section 15.3.3)   ");
    ESP_LOGE(TAG, "  Core dump will be saved to flash and uploaded on reboot ");
    ESP_LOGE(TAG, "==========================================================");
    vTaskDelay(pdMS_TO_TICKS(500));
    volatile int *null_ptr = NULL;
    *null_ptr = 0xDEADBEEF;
}

static void push_btn_cb(void *arg)
{
    bool new_state = !g_output_state;
    ESP_LOGI(TAG, "==> [Physical Button Click]: Toggling light %s", new_state ? "ON" : "OFF");
    app_driver_set_state(new_state);

    if (light_device) {
        esp_rmaker_param_t *power_param = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_POWER_NAME);
        if (power_param) {
            esp_rmaker_param_update_and_report(power_param, esp_rmaker_bool(g_output_state));
        }
    }
}

static void double_click_cb(void *arg)
{
    if (!g_output_state) {
        app_driver_set_state(true);
        if (light_device) {
            esp_rmaker_param_t *p = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_POWER_NAME);
            if (p) esp_rmaker_param_update_and_report(p, esp_rmaker_bool(true));
        }
    }
    s_hue_index = (s_hue_index + 1) % NUM_PRESET_HUES;
    g_hue = s_preset_hues[s_hue_index];
    g_saturation = 100;
    ESP_LOGI(TAG, "==> [Physical Button Double Click]: Cycle Hue -> %d", g_hue);
    light_driver_set_hsv(g_hue, g_saturation, g_brightness);

    /* Telemetry: Record custom diagnostic event */
    ESP_DIAG_EVENT("LIGHT_EVENT", "Hue cycled via double-click: Hue = %d, Sat = %d", g_hue, g_saturation);

    if (light_device) {
        esp_rmaker_param_t *hue_p = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_HUE_NAME);
        if (hue_p) esp_rmaker_param_update_and_report(hue_p, esp_rmaker_int(g_hue));
        esp_rmaker_param_t *sat_p = esp_rmaker_device_get_param_by_name(light_device, ESP_RMAKER_DEF_SATURATION_NAME);
        if (sat_p) esp_rmaker_param_update_and_report(sat_p, esp_rmaker_int(g_saturation));
    }
}

static void button_repeat_cb(void *arg)
{
    button_handle_t btn = (button_handle_t)arg;
    uint8_t repeat = iot_button_get_repeat(btn);
    ESP_LOGI(TAG, "==> [Button Repeat]: Click Count = %d", repeat);
    if (repeat >= 5) {
        app_trigger_crash_test();
    }
}

static void factory_reset_trigger(void *arg)
{
    ESP_LOGW(TAG, "==> [Physical Button Long Press]: Resetting Wi-Fi & Factory Reset in %d seconds...", REBOOT_DELAY);
    light_driver_blink_start(255, 0, 0);
    esp_rmaker_factory_reset(0, REBOOT_DELAY);
}

void app_driver_init(void)
{
    /* 1. Configure push button */
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num     = LIGHT_BUTTON_GPIO,
            .active_level = LIGHT_BUTTON_ACTIVE_LEVEL,
        },
    };
    button_handle_t btn_handle = iot_button_create(&btn_cfg);
    if (btn_handle) {
        /* Single click: Toggle Light ON / OFF */
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, push_btn_cb);
        /* Double click: Cycle Hue Presets */
        iot_button_register_cb(btn_handle, BUTTON_DOUBLE_CLICK, double_click_cb);
        /* 5-click repeat: Artificial Crash Test (Section 15.3.3) */
        iot_button_register_cb(btn_handle, BUTTON_PRESS_REPEAT, button_repeat_cb);
        /* Long press: Factory Reset */
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, factory_reset_trigger);
    }

    /* 2. Configure WS2812B NeoPixel 8-LED strip via Hardware SPI2 DMA @ 3.2MHz */
    ESP_LOGI(TAG, "Initializing WS2812B Hardware SPI2 DMA (GPIO %d, %d LEDs)...", LIGHT_WS2818_GPIO, LIGHT_WS2818_NUM_LEDS);
    light_driver_config_t driver_config = {
        .gpio_ws2812 = LIGHT_WS2818_GPIO,
        .num_leds    = LIGHT_WS2818_NUM_LEDS,
    };
    ESP_ERROR_CHECK(light_driver_init(&driver_config));

    /* 3. Initialize default light state and acquire PM lock */
    app_driver_set_state(g_output_state);
}

int IRAM_ATTR app_driver_set_state(bool state)
{
    g_output_state = state;
    if (g_output_state) {
        app_pm_lock_acquire();
        light_driver_set_hsv(g_hue, g_saturation, g_brightness);
        ESP_LOGI(TAG, "Light ON (HSV: %d, %d, %d)", g_hue, g_saturation, g_brightness);
        ESP_DIAG_EVENT("LIGHT_EVENT", "Light turned ON (HSV: %d, %d, %d)", g_hue, g_saturation, g_brightness);
    } else {
        light_driver_set_switch(false);
        app_pm_lock_release();
        ESP_LOGI(TAG, "Light OFF (PM lock released, automatic Light-sleep eligible)");
        ESP_DIAG_EVENT("LIGHT_EVENT", "Light turned OFF");
    }
    return ESP_OK;
}

bool app_driver_get_state(void)
{
    return g_output_state;
}

esp_err_t app_light_set_power(bool power)
{
    return app_driver_set_state(power);
}

esp_err_t app_light_set_brightness(uint16_t brightness)
{
    g_brightness = (uint8_t)brightness;
    if (g_output_state) {
        light_driver_set_hsv(g_hue, g_saturation, g_brightness);
    }
    ESP_DIAG_EVENT("LIGHT_EVENT", "Brightness set to %d", g_brightness);
    return ESP_OK;
}

esp_err_t app_light_set_hue(uint16_t hue)
{
    g_hue = hue;
    if (g_output_state) {
        light_driver_set_hsv(g_hue, g_saturation, g_brightness);
    }
    ESP_DIAG_EVENT("LIGHT_EVENT", "Hue set to %d", g_hue);
    return ESP_OK;
}

esp_err_t app_light_set_saturation(uint16_t saturation)
{
    g_saturation = (uint8_t)saturation;
    if (g_output_state) {
        light_driver_set_hsv(g_hue, g_saturation, g_brightness);
    }
    ESP_DIAG_EVENT("LIGHT_EVENT", "Saturation set to %d", g_saturation);
    return ESP_OK;
}

esp_err_t app_light_set(uint32_t hue, uint32_t saturation, uint32_t brightness)
{
    g_hue = (uint16_t)hue;
    g_saturation = (uint8_t)saturation;
    g_brightness = (uint8_t)brightness;
    if (g_output_state) {
        light_driver_set_hsv(g_hue, g_saturation, g_brightness);
    }
    ESP_DIAG_EVENT("LIGHT_EVENT", "Color set to Hue=%d, Sat=%d, Bri=%d", g_hue, g_saturation, g_brightness);
    return ESP_OK;
}
