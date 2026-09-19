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

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "soc/soc_caps.h"

#include "button_adc.h"

static const char *TAG = "adc button";

#define ADC_BTN_CHECK(a, str, ret_val)                            \
    if (!(a))                                                     \
    {                                                             \
        ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, str); \
        return (ret_val);                                         \
    }

#define NO_OF_SAMPLES   CONFIG_ADC_BUTTON_SAMPLE_TIMES

/*
 * ESP32-C3 does not have ADC_ATTEN_DB_11.
 * Use the highest available attenuation.
 */
#define ADC_BUTTON_ATTEN        ADC_ATTEN_DB_12

#define ADC_BUTTON_ADC_UNIT     ADC_UNIT_1
#define ADC_BUTTON_MAX_CHANNEL  CONFIG_ADC_BUTTON_MAX_CHANNEL
#define ADC_BUTTON_MAX_BUTTON   CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL

typedef struct {
    uint16_t min;
    uint16_t max;
} button_data_t;

typedef struct {
    adc_channel_t channel;
    uint8_t is_init;
    button_data_t btns[ADC_BUTTON_MAX_BUTTON];
    uint64_t last_time;
} btn_adc_channel_t;

typedef struct {
    bool is_configured;

    /* ADC oneshot handle */
    adc_oneshot_unit_handle_t adc_handle;

    /* ADC calibration handle */
    adc_cali_handle_t cali_handle;

    btn_adc_channel_t ch[ADC_BUTTON_MAX_CHANNEL];

    uint8_t ch_num;
} adc_button_t;

static adc_button_t g_button = {0};


/**
 * @brief Find an unused channel slot
 */
static int find_unused_channel(void)
{
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {

        if (g_button.ch[i].is_init == 0) {
            return i;
        }
    }

    return -1;
}


/**
 * @brief Find channel slot
 */
static int find_channel(adc_channel_t channel)
{
    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {

        if (channel == g_button.ch[i].channel &&
            g_button.ch[i].is_init) {

            return i;
        }
    }

    return -1;
}


/**
 * @brief Initialize ADC calibration
 *
 * Try curve fitting first.
 * If not supported, try line fitting.
 */
static bool adc_calibration_init(adc_unit_t unit,
                                  adc_channel_t channel,
                                  adc_atten_t atten,
                                  adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED

    ESP_LOGI(TAG, "Calibration scheme: Curve Fitting");

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ret = adc_cali_create_scheme_curve_fitting(
        &cali_config,
        &handle
    );

    if (ret == ESP_OK) {
        calibrated = true;
    }

#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

    if (!calibrated) {

        ESP_LOGI(TAG, "Calibration scheme: Line Fitting");

        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };

        ret = adc_cali_create_scheme_line_fitting(
            &cali_config,
            &handle
        );

        if (ret == ESP_OK) {
            calibrated = true;
        }
    }

#endif

    *out_handle = handle;

    if (ret == ESP_OK) {

        ESP_LOGI(
            TAG,
            "ADC calibration success"
        );

    } else if (ret == ESP_ERR_NOT_SUPPORTED) {

        ESP_LOGW(
            TAG,
            "ADC calibration is not supported. "
            "Using raw ADC value."
        );

    } else {

        ESP_LOGW(
            TAG,
            "ADC calibration initialization failed: %s",
            esp_err_to_name(ret)
        );
    }

    return calibrated;
}


/**
 * @brief Deinitialize ADC calibration
 */
static void adc_calibration_deinit(adc_cali_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED

    adc_cali_delete_scheme_curve_fitting(handle);

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

    adc_cali_delete_scheme_line_fitting(handle);

#endif
}


/**
 * @brief Initialize ADC button
 */
esp_err_t button_adc_init(const button_adc_config_t *config)
{
    ADC_BTN_CHECK(
        config != NULL,
        "Pointer of config is invalid",
        ESP_ERR_INVALID_ARG
    );

    /*
     * ADC_CHANNEL_MAX was removed/not exposed for this target
     * in the new ESP-IDF ADC driver.
     *
     * Use the number of ADC channels supported by ADC_UNIT_1.
     */
    ADC_BTN_CHECK(
        config->adc_channel >= 0 &&
        config->adc_channel < SOC_ADC_CHANNEL_NUM(ADC_BUTTON_ADC_UNIT),
        "channel out of range",
        ESP_ERR_NOT_SUPPORTED
    );

    ADC_BTN_CHECK(
        config->button_index < ADC_BUTTON_MAX_BUTTON,
        "button_index out of range",
        ESP_ERR_NOT_SUPPORTED
    );

    ADC_BTN_CHECK(
        config->max > 0,
        "key max voltage invalid",
        ESP_ERR_INVALID_ARG
    );

    int ch_index = find_channel(config->adc_channel);

    if (ch_index >= 0) {

        /* Channel already initialized */
        ADC_BTN_CHECK(
            g_button.ch[ch_index]
                .btns[config->button_index]
                .max == 0,
            "The button_index has been used",
            ESP_ERR_INVALID_STATE
        );

    } else {

        /* New channel */
        int unused_ch_index = find_unused_channel();

        ADC_BTN_CHECK(
            unused_ch_index >= 0,
            "exceed max channel number, can't create a new channel",
            ESP_ERR_INVALID_STATE
        );

        ch_index = unused_ch_index;
    }


    /**
     * Initialize ADC unit
     */
    if (g_button.is_configured == 0) {

        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_BUTTON_ADC_UNIT,
        };

        esp_err_t ret = adc_oneshot_new_unit(
            &init_config,
            &g_button.adc_handle
        );

        ADC_BTN_CHECK(
            ret == ESP_OK,
            "adc_oneshot_new_unit failed",
            ret
        );


        /**
         * Initialize ADC calibration
         */
        adc_calibration_init(
            ADC_BUTTON_ADC_UNIT,
            config->adc_channel,
            ADC_BUTTON_ATTEN,
            &g_button.cali_handle
        );

        g_button.is_configured = true;
    }


    /**
     * Initialize ADC channel
     */
    if (g_button.ch[ch_index].is_init == 0) {

        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_BUTTON_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };

        esp_err_t ret = adc_oneshot_config_channel(
            g_button.adc_handle,
            config->adc_channel,
            &chan_config
        );

        ADC_BTN_CHECK(
            ret == ESP_OK,
            "adc_oneshot_config_channel failed",
            ret
        );

        g_button.ch[ch_index].channel = config->adc_channel;
        g_button.ch[ch_index].is_init = 1;
        g_button.ch[ch_index].last_time = 0;
    }


    /**
     * Save button voltage range
     */
    g_button.ch[ch_index]
        .btns[config->button_index]
        .max = config->max;

    g_button.ch[ch_index]
        .btns[config->button_index]
        .min = config->min;

    g_button.ch_num++;

    return ESP_OK;
}


/**
 * @brief Deinitialize ADC button
 */
esp_err_t button_adc_deinit(adc_channel_t channel, int button_index)
{
    ADC_BTN_CHECK(
        channel >= 0 &&
        channel < SOC_ADC_CHANNEL_NUM(ADC_BUTTON_ADC_UNIT),
        "channel out of range",
        ESP_ERR_INVALID_ARG
    );

    ADC_BTN_CHECK(
        button_index >= 0 &&
        button_index < ADC_BUTTON_MAX_BUTTON,
        "button_index out of range",
        ESP_ERR_INVALID_ARG
    );

    int ch_index = find_channel(channel);

    ADC_BTN_CHECK(
        ch_index >= 0,
        "can't find the channel",
        ESP_ERR_INVALID_ARG
    );


    g_button.ch[ch_index]
        .btns[button_index]
        .max = 0;

    g_button.ch[ch_index]
        .btns[button_index]
        .min = 0;


    /**
     * Check button usage on this channel
     */
    uint8_t unused_button = 0;

    for (size_t i = 0; i < ADC_BUTTON_MAX_BUTTON; i++) {

        if (g_button.ch[ch_index].btns[i].max == 0) {
            unused_button++;
        }
    }


    /**
     * If all buttons are unused,
     * mark this channel as unused.
     */
    if (unused_button == ADC_BUTTON_MAX_BUTTON &&
        g_button.ch[ch_index].is_init) {

        g_button.ch[ch_index].is_init = 0;

        /*
         * ADC_CHANNEL_MAX no longer exists.
         * Use an invalid value for the unused slot.
         */
        g_button.ch[ch_index].channel = -1;

        g_button.ch_num--;

        ESP_LOGD(
            TAG,
            "all buttons are unused on channel %d",
            channel
        );
    }


    /**
     * Check ADC channel usage
     */
    uint8_t unused_ch = 0;

    for (size_t i = 0; i < ADC_BUTTON_MAX_CHANNEL; i++) {

        if (g_button.ch[i].is_init == 0) {
            unused_ch++;
        }
    }


    /**
     * If all channels are unused,
     * deinitialize ADC peripheral.
     */
    if (unused_ch == ADC_BUTTON_MAX_CHANNEL &&
        g_button.is_configured) {

        if (g_button.cali_handle != NULL) {

            adc_calibration_deinit(
                g_button.cali_handle
            );
        }

        if (g_button.adc_handle != NULL) {

            adc_oneshot_del_unit(
                g_button.adc_handle
            );
        }

        memset(
            &g_button,
            0,
            sizeof(adc_button_t)
        );

        ESP_LOGD(
            TAG,
            "all channels are unused, deinit ADC"
        );
    }

    return ESP_OK;
}


/**
 * @brief Read ADC voltage in mV
 */
static uint32_t get_adc_voltage(adc_channel_t channel)
{
    uint32_t voltage = 0;

    uint32_t adc_sum = 0;


    /**
     * Multisampling
     */
    for (int i = 0; i < NO_OF_SAMPLES; i++) {

        int adc_raw = 0;

        esp_err_t ret = adc_oneshot_read(
            g_button.adc_handle,
            channel,
            &adc_raw
        );

        if (ret != ESP_OK) {

            ESP_LOGE(
                TAG,
                "adc_oneshot_read failed: %s",
                esp_err_to_name(ret)
            );

            return 0;
        }

        adc_sum += adc_raw;
    }


    adc_sum /= NO_OF_SAMPLES;


    /**
     * Convert raw ADC value to voltage.
     */
    if (g_button.cali_handle != NULL) {

        int calibrated_voltage = 0;

        esp_err_t ret = adc_cali_raw_to_voltage(
            g_button.cali_handle,
            adc_sum,
            &calibrated_voltage
        );

        if (ret == ESP_OK) {

            voltage = calibrated_voltage;

        } else {

            ESP_LOGW(
                TAG,
                "ADC calibration failed: %s",
                esp_err_to_name(ret)
            );

            voltage = adc_sum;
        }

    } else {

        /**
         * No calibration available.
         *
         * Return raw ADC value.
         */
        voltage = adc_sum;
    }


    ESP_LOGV(
        TAG,
        "Raw: %lu\tVoltage: %lu mV",
        (unsigned long)adc_sum,
        (unsigned long)voltage
    );

    return voltage;
}


/**
 * @brief Get ADC button level
 */
uint8_t button_adc_get_key_level(void *button_index)
{
    static uint16_t vol = 0;

    uint32_t ch =
        ADC_BUTTON_SPLIT_CHANNEL(button_index);

    uint32_t index =
        ADC_BUTTON_SPLIT_INDEX(button_index);


    ADC_BTN_CHECK(
        ch < SOC_ADC_CHANNEL_NUM(ADC_BUTTON_ADC_UNIT),
        "channel out of range",
        0
    );

    ADC_BTN_CHECK(
        index < ADC_BUTTON_MAX_BUTTON,
        "button_index out of range",
        0
    );


    int ch_index = find_channel(ch);

    ADC_BTN_CHECK(
        ch_index >= 0,
        "The button_index is not init",
        0
    );


    /**
     * Start sampling only when more than 1 ms
     * has elapsed since the previous sample.
     */
    if ((esp_timer_get_time() -
         g_button.ch[ch_index].last_time) > 1000) {

        vol = get_adc_voltage(ch);

        g_button.ch[ch_index].last_time =
            esp_timer_get_time();
    }


    /**
     * Check whether voltage is inside
     * the button's configured range.
     */
    if (vol <=
            g_button.ch[ch_index].btns[index].max &&
        vol >
            g_button.ch[ch_index].btns[index].min) {

        return 1;
    }


    return 0;
}