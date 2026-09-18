
// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gptimer.h"
#include "driver/ledc.h"

#include "iot_led.h"

#define LEDC_FADE_MARGIN        (10)
#define LEDC_TIMER_PRECISION   (13)

#define LEDC_VALUE_TO_DUTY(value) \
    ((uint32_t)((value) * ((1U << LEDC_TIMER_PRECISION)) / UINT16_MAX))

#define LEDC_FIXED_Q            (8)

#define FLOATINT_2_FIXED(X, Q) \
    ((int)((X) * (0x1U << (Q))))

#define FIXED_2_FLOATING(X, Q) \
    ((int)((X) / (0x1U << (Q))))

#define GET_FIXED_INTEGER_PART(X, Q) \
    ((X) >> (Q))

#define GET_FIXED_DECIMAL_PART(X, Q) \
    ((X) & ((0x1U << (Q)) - 1))


typedef struct {
    int cur;
    int final;
    int step;
    int cycle;
    size_t num;
} ledc_fade_data_t;


/*
 * ESP-IDF 6.x:
 * Legacy timer_group/timer.h was removed.
 * GPTimer is used instead.
 */
typedef struct {
    gptimer_handle_t timer;
} hw_timer_idx_t;


typedef struct {
    ledc_fade_data_t fade_data[LEDC_CHANNEL_MAX];

    ledc_mode_t speed_mode;

    ledc_timer_t timer_num;

    hw_timer_idx_t timer_id;

    TaskHandle_t fade_task;

    volatile bool task_running;
    volatile bool timer_started;
} iot_light_t;


static const char *TAG = "iot_led";

static iot_light_t *g_light_config = NULL;

static uint16_t *g_gamma_table = NULL;


/*
 * Forward declarations.
 */
static bool fade_timercb(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *user_ctx);

static void light_fade_task(void *arg);


/*
 * Create GPTimer.
 *
 * Resolution:
 *     1 MHz
 *
 * Therefore:
 *     1 tick = 1 us
 */
static esp_err_t iot_timer_create(
    hw_timer_idx_t *timer_id,
    bool auto_reload,
    uint32_t timer_interval_ms)
{
    if (timer_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };

    esp_err_t ret = gptimer_new_timer(
        &timer_config,
        &timer_id->timer
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "gptimer_new_timer failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    gptimer_alarm_config_t alarm_config = {
        .alarm_count = (uint64_t)timer_interval_ms * 1000ULL,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = auto_reload,
    };

    ret = gptimer_set_alarm_action(
        timer_id->timer,
        &alarm_config
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "gptimer_set_alarm_action failed: %s",
            esp_err_to_name(ret)
        );

        gptimer_del_timer(timer_id->timer);
        timer_id->timer = NULL;

        return ret;
    }


    /*
     * GPTimer callback only notifies the fade task.
     * No LEDC operation is performed inside the ISR.
     */
    gptimer_event_callbacks_t callbacks = {
        .on_alarm = fade_timercb,
    };

    ret = gptimer_register_event_callbacks(
        timer_id->timer,
        &callbacks,
        NULL
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "gptimer_register_event_callbacks failed: %s",
            esp_err_to_name(ret)
        );

        gptimer_del_timer(timer_id->timer);
        timer_id->timer = NULL;

        return ret;
    }


    ret = gptimer_enable(timer_id->timer);

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "gptimer_enable failed: %s",
            esp_err_to_name(ret)
        );

        gptimer_del_timer(timer_id->timer);
        timer_id->timer = NULL;

        return ret;
    }

    return ESP_OK;
}


/*
 * Start GPTimer.
 */
static esp_err_t iot_timer_start(
    hw_timer_idx_t *timer_id)
{
    if (timer_id == NULL || timer_id->timer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Reset timer counter before every new start.
     */
    esp_err_t ret = gptimer_set_raw_count(
        timer_id->timer,
        0
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "gptimer_set_raw_count failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    ret = gptimer_start(timer_id->timer);

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "gptimer_start failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    if (g_light_config != NULL) {
        g_light_config->timer_started = true;
    }

    return ESP_OK;
}


/*
 * Stop GPTimer.
 */
static esp_err_t iot_timer_stop(
    hw_timer_idx_t *timer_id)
{
    if (timer_id == NULL || timer_id->timer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = gptimer_stop(timer_id->timer);

    if (ret != ESP_OK &&
        ret != ESP_ERR_INVALID_STATE) {

        ESP_LOGE(
            TAG,
            "gptimer_stop failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    if (g_light_config != NULL) {
        g_light_config->timer_started = false;
    }

    return ESP_OK;
}


/*
 * Set LEDC duty.
 *
 * This function is executed from the normal FreeRTOS task
 * context, NOT from the GPTimer ISR.
 */
static esp_err_t iot_ledc_set_duty(
    ledc_mode_t speed_mode,
    ledc_channel_t channel,
    uint32_t duty)
{
    esp_err_t ret;

    ret = ledc_set_duty(
        speed_mode,
        channel,
        duty
    );

    if (ret != ESP_OK) {
        return ret;
    }

    ret = ledc_update_duty(
        speed_mode,
        channel
    );

    return ret;
}


/*
 * Create gamma correction table.
 */
static void gamma_table_create(
    uint16_t *gamma_table,
    float correction)
{
    if (gamma_table == NULL || correction <= 0.0f) {
        return;
    }

    for (int i = 0; i < GAMMA_TABLE_SIZE; i++) {

        float value_tmp =
            (float)i /
            (GAMMA_TABLE_SIZE - 1);

        value_tmp =
            powf(
                value_tmp,
                1.0f / correction
            );

        gamma_table[i] =
            (uint16_t)FLOATINT_2_FIXED(
                value_tmp * GAMMA_TABLE_SIZE,
                LEDC_FIXED_Q
            );
    }


    if (gamma_table[GAMMA_TABLE_SIZE - 1] == 0) {
        gamma_table[GAMMA_TABLE_SIZE - 1] = UINT16_MAX;
    }
}


/*
 * Convert gamma value to LEDC duty.
 */
static uint32_t gamma_value_to_duty(
    int value)
{
    if (g_gamma_table == NULL) {
        return 0;
    }


    if (value < 0) {
        value = 0;
    }


    int max_value =
        (GAMMA_TABLE_SIZE - 1) << LEDC_FIXED_Q;

    if (value > max_value) {
        value = max_value;
    }


    uint32_t tmp_q =
        GET_FIXED_INTEGER_PART(
            value,
            LEDC_FIXED_Q
        );

    uint32_t tmp_r =
        GET_FIXED_DECIMAL_PART(
            value,
            LEDC_FIXED_Q
        );


    uint32_t cur =
        LEDC_VALUE_TO_DUTY(
            g_gamma_table[tmp_q]
        );


    uint32_t next;

    if (tmp_q < (GAMMA_TABLE_SIZE - 1)) {

        next =
            LEDC_VALUE_TO_DUTY(
                g_gamma_table[tmp_q + 1]
            );

    } else {

        next = cur;
    }


    uint32_t tmp =
        cur +
        ((next - cur) * tmp_r) /
        (0x1U << LEDC_FIXED_Q);


    return tmp;
}


/*
 * Apply one fade step to one LED channel.
 */
static void process_fade_channel(
    ledc_channel_t channel)
{
    if (g_light_config == NULL) {
        return;
    }


    ledc_fade_data_t *fade_data =
        &g_light_config->fade_data[channel];


    /*
     * Normal fade operation.
     */
    if (fade_data->num > 0) {

        fade_data->num--;


        if (fade_data->step != 0) {

            fade_data->cur +=
                fade_data->step;


            /*
             * Make sure the current value does not
             * overshoot the final value.
             */
            if (fade_data->step > 0 &&
                fade_data->cur > fade_data->final) {

                fade_data->cur =
                    fade_data->final;
            }


            if (fade_data->step < 0 &&
                fade_data->cur < fade_data->final) {

                fade_data->cur =
                    fade_data->final;
            }
        }


        /*
         * Last step -> force exact final value.
         */
        if (fade_data->num == 0) {

            fade_data->cur =
                fade_data->final;
        }


        iot_ledc_set_duty(
            g_light_config->speed_mode,
            channel,
            gamma_value_to_duty(
                fade_data->cur
            )
        );

        return;
    }


    /*
     * Blink operation.
     */
    if (fade_data->cycle > 0) {

        fade_data->num =
            fade_data->cycle - 1;


        if (fade_data->step != 0) {

            fade_data->step *= -1;

            fade_data->cur +=
                fade_data->step;

        } else {

            if (fade_data->cur ==
                fade_data->final) {

                fade_data->cur = 0;

            } else {

                fade_data->cur =
                    fade_data->final;
            }
        }


        iot_ledc_set_duty(
            g_light_config->speed_mode,
            channel,
            gamma_value_to_duty(
                fade_data->cur
            )
        );
    }
}


/*
 * GPTimer ISR callback.
 *
 * IMPORTANT:
 * Do NOT perform LEDC operations here.
 *
 * Only notify the FreeRTOS task.
 */
static bool fade_timercb(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;


    if (g_light_config == NULL ||
        g_light_config->fade_task == NULL) {

        return false;
    }


    BaseType_t high_task_woken = pdFALSE;


    vTaskNotifyGiveFromISR(
        g_light_config->fade_task,
        &high_task_woken
    );


    return high_task_woken == pdTRUE;
}


/*
 * FreeRTOS task which performs the actual LED fade/blink work.
 */
static void light_fade_task(void *arg)
{
    iot_light_t *light =
        (iot_light_t *)arg;


    while (light->task_running) {

        /*
         * Wait until GPTimer generates an alarm.
         */
        ulTaskNotifyTake(
            pdTRUE,
            portMAX_DELAY
        );


        if (!light->task_running) {
            break;
        }


        bool all_idle = true;


        for (int channel = 0;
             channel < LEDC_CHANNEL_MAX;
             channel++) {

            ledc_fade_data_t *fade_data =
                &light->fade_data[channel];


            if (fade_data->num > 0 ||
                fade_data->cycle > 0) {

                all_idle = false;

                process_fade_channel(
                    (ledc_channel_t)channel
                );
            }
        }


        /*
         * No channel is doing fade/blink anymore.
         */
        if (all_idle &&
            light->timer_started) {

            iot_timer_stop(
                &light->timer_id
            );
        }
    }


    vTaskDelete(NULL);
}


/*
 * Initialize LED.
 */
esp_err_t iot_led_init(
    ledc_timer_t timer_num,
    ledc_mode_t speed_mode,
    uint32_t freq_hz,
    ledc_clk_cfg_t clk_cfg,
    ledc_timer_bit_t duty_resolution)
{
    esp_err_t ret = ESP_OK;


    /*
     * ESP32-C3 supports LOW SPEED LEDC only.
     */
    if (speed_mode != LEDC_LOW_SPEED_MODE) {

        ESP_LOGE(
            TAG,
            "ESP32-C3 requires LEDC_LOW_SPEED_MODE"
        );

        return ESP_ERR_INVALID_ARG;
    }


    const ledc_timer_config_t ledc_time_config = {

        .speed_mode =
            speed_mode,

        .duty_resolution =
            duty_resolution,

        .timer_num =
            timer_num,

        .freq_hz =
            freq_hz,

        .clk_cfg =
            clk_cfg,
    };


    ret = ledc_timer_config(
        &ledc_time_config
    );

    LIGHT_ERROR_CHECK(
        ret != ESP_OK,
        ret,
        "LEDC timer configuration"
    );


    /*
     * Create gamma table only once.
     */
    if (g_gamma_table == NULL) {

        g_gamma_table =
            calloc(
                GAMMA_TABLE_SIZE + 1,
                sizeof(uint16_t)
            );

        if (g_gamma_table == NULL) {
            return ESP_ERR_NO_MEM;
        }


        gamma_table_create(
            g_gamma_table,
            GAMMA_CORRECTION
        );

    } else {

        ESP_LOGW(
            TAG,
            "gamma_table has already been initialized"
        );
    }


    if (g_light_config != NULL) {

        ESP_LOGW(
            TAG,
            "g_light_config has already been initialized"
        );

        return ESP_OK;
    }


    g_light_config =
        calloc(
            1,
            sizeof(iot_light_t)
        );


    if (g_light_config == NULL) {

        free(g_gamma_table);
        g_gamma_table = NULL;

        return ESP_ERR_NO_MEM;
    }


    g_light_config->timer_num =
        timer_num;

    g_light_config->speed_mode =
        speed_mode;

    g_light_config->timer_id.timer =
        NULL;

    g_light_config->timer_started =
        false;

    g_light_config->task_running =
        true;


    /*
     * Create GPTimer.
     */
    ret = iot_timer_create(
        &g_light_config->timer_id,
        true,
        DUTY_SET_CYCLE
    );


    if (ret != ESP_OK) {

        free(g_light_config);
        g_light_config = NULL;

        free(g_gamma_table);
        g_gamma_table = NULL;

        return ret;
    }


    /*
     * Create fade task.
     */
    BaseType_t task_ret =
        xTaskCreate(
            light_fade_task,
            "light_fade",
            4096,
            g_light_config,
            5,
            &g_light_config->fade_task
        );


    if (task_ret != pdPASS) {

        gptimer_disable(
            g_light_config->timer_id.timer
        );

        gptimer_del_timer(
            g_light_config->timer_id.timer
        );

        g_light_config->timer_id.timer =
            NULL;

        free(g_light_config);
        g_light_config = NULL;

        free(g_gamma_table);
        g_gamma_table = NULL;

        return ESP_ERR_NO_MEM;
    }


    return ESP_OK;
}


/*
 * Deinitialize LED.
 */
esp_err_t iot_led_deinit()
{
    if (g_light_config == NULL) {

        if (g_gamma_table != NULL) {

            free(g_gamma_table);
            g_gamma_table = NULL;
        }

        return ESP_OK;
    }


    /*
     * Tell fade task to stop.
     */
    g_light_config->task_running =
        false;


    if (g_light_config->fade_task != NULL) {

        xTaskNotifyGive(
            g_light_config->fade_task
        );

        /*
         * Give the task a short time to exit.
         */
        vTaskDelay(
            pdMS_TO_TICKS(10)
        );

        g_light_config->fade_task =
            NULL;
    }


    /*
     * Stop GPTimer.
     */
    if (g_light_config->timer_id.timer != NULL) {

        gptimer_stop(
            g_light_config->timer_id.timer
        );

        gptimer_disable(
            g_light_config->timer_id.timer
        );

        gptimer_del_timer(
            g_light_config->timer_id.timer
        );

        g_light_config->timer_id.timer =
            NULL;
    }


    free(g_light_config);
    g_light_config = NULL;


    if (g_gamma_table != NULL) {

        free(g_gamma_table);
        g_gamma_table = NULL;
    }


    return ESP_OK;
}


/*
 * Register LEDC channel.
 */
esp_err_t iot_led_regist_channel(
    ledc_channel_t channel,
    gpio_num_t gpio_num)
{
    esp_err_t ret = ESP_OK;


    LIGHT_ERROR_CHECK(
        g_light_config == NULL,
        ESP_ERR_INVALID_ARG,
        "iot_led_init() must be called first"
    );


#ifdef CONFIG_SPIRAM_SUPPORT

    /*
     * GPIO16 and GPIO17 are reserved for PSRAM
     * on ESP32 variants where PSRAM uses those pins.
     */
    if (gpio_num == GPIO_NUM_16 ||
        gpio_num == GPIO_NUM_17) {

        return ESP_ERR_INVALID_ARG;
    }

#endif


    const ledc_channel_config_t ledc_ch_config = {

        .gpio_num =
            gpio_num,

        .channel =
            channel,

        .intr_type =
            LEDC_INTR_DISABLE,

        .speed_mode =
            g_light_config->speed_mode,

        .timer_sel =
            g_light_config->timer_num,

        .duty =
            0,

        .hpoint =
            0,

#if SOC_LEDC_SUPPORT_SLEEP_RETENTION
        .sleep_mode =
            LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
#endif

    };


    ret = ledc_channel_config(
        &ledc_ch_config
    );


    LIGHT_ERROR_CHECK(
        ret != ESP_OK,
        ret,
        "LEDC channel configuration"
    );


    /*
     * Initialize internal state.
     */
    g_light_config
        ->fade_data[channel]
        .cur = 0;

    g_light_config
        ->fade_data[channel]
        .final = 0;

    g_light_config
        ->fade_data[channel]
        .step = 0;

    g_light_config
        ->fade_data[channel]
        .cycle = 0;

    g_light_config
        ->fade_data[channel]
        .num = 0;


    return ESP_OK;
}


/*
 * Get current LED channel value.
 */
esp_err_t iot_led_get_channel(
    ledc_channel_t channel,
    uint8_t *dst)
{
    LIGHT_ERROR_CHECK(
        g_light_config == NULL,
        ESP_ERR_INVALID_ARG,
        "iot_led_init() must be called first"
    );


    LIGHT_ERROR_CHECK(
        dst == NULL,
        ESP_ERR_INVALID_ARG,
        "dst should not be NULL"
    );


    if (channel >= LEDC_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }


    int cur =
        g_light_config
        ->fade_data[channel]
        .cur;


    *dst =
        (uint8_t)FIXED_2_FLOATING(
            cur,
            LEDC_FIXED_Q
        );


    return ESP_OK;
}


/*
 * Set LED channel value.
 */
esp_err_t iot_led_set_channel(
    ledc_channel_t channel,
    uint8_t value,
    uint32_t fade_ms)
{
    LIGHT_ERROR_CHECK(
        g_light_config == NULL,
        ESP_ERR_INVALID_ARG,
        "iot_led_init() must be called first"
    );


    if (channel >= LEDC_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }


    ledc_fade_data_t *fade_data =
        &g_light_config->fade_data[channel];


    fade_data->final =
        FLOATINT_2_FIXED(
            value,
            LEDC_FIXED_Q
        );


    if (fade_ms < DUTY_SET_CYCLE) {

        fade_data->num = 1;

    } else {

        fade_data->num =
            fade_ms /
            DUTY_SET_CYCLE;

        if (fade_data->num == 0) {
            fade_data->num = 1;
        }
    }


    int difference =
        fade_data->final -
        fade_data->cur;


    fade_data->step =
        difference /
        (int)fade_data->num;


    /*
     * Avoid a zero step for a non-zero fade.
     */
    if (difference != 0 &&
        fade_data->step == 0) {

        fade_data->step =
            difference > 0
            ? 1
            : -1;
    }


    /*
     * Stop blinking when normal set_channel
     * is requested.
     */
    fade_data->cycle = 0;


    /*
     * If already at the requested value,
     * no timer is necessary.
     */
    if (fade_data->cur ==
        fade_data->final) {

        fade_data->num = 0;

        iot_ledc_set_duty(
            g_light_config->speed_mode,
            channel,
            gamma_value_to_duty(
                fade_data->cur
            )
        );

        return ESP_OK;
    }


    if (!g_light_config->timer_started) {

        return iot_timer_start(
            &g_light_config->timer_id
        );
    }


    return ESP_OK;
}


/*
 * Start blinking LED.
 */
esp_err_t iot_led_start_blink(
    ledc_channel_t channel,
    uint8_t value,
    uint32_t period_ms,
    bool fade_flag)
{
    LIGHT_ERROR_CHECK(
        g_light_config == NULL,
        ESP_ERR_INVALID_ARG,
        "iot_led_init() must be called first"
    );


    if (channel >= LEDC_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }


    if (period_ms <
        (DUTY_SET_CYCLE * 2)) {

        return ESP_ERR_INVALID_ARG;
    }


    ledc_fade_data_t *fade_data =
        &g_light_config->fade_data[channel];


    fade_data->final =
        FLOATINT_2_FIXED(
            value,
            LEDC_FIXED_Q
        );


    fade_data->cur =
        fade_data->final;


    fade_data->cycle =
        period_ms /
        2 /
        DUTY_SET_CYCLE;


    if (fade_data->cycle == 0) {
        fade_data->cycle = 1;
    }


    if (fade_flag) {

        fade_data->num =
            fade_data->cycle;


        fade_data->step =
            fade_data->cur /
            (int)fade_data->num;

        fade_data->step *= -1;

    } else {

        fade_data->num = 0;
        fade_data->step = 0;
    }


    /*
     * Set initial brightness immediately.
     */
    iot_ledc_set_duty(
        g_light_config->speed_mode,
        channel,
        gamma_value_to_duty(
            fade_data->cur
        )
    );


    if (!g_light_config->timer_started) {

        return iot_timer_start(
            &g_light_config->timer_id
        );
    }


    return ESP_OK;
}


/*
 * Stop blinking LED.
 */
esp_err_t iot_led_stop_blink(
    ledc_channel_t channel)
{
    LIGHT_ERROR_CHECK(
        g_light_config == NULL,
        ESP_ERR_INVALID_ARG,
        "iot_led_init() must be called first"
    );


    if (channel >= LEDC_CHANNEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }


    ledc_fade_data_t *fade_data =
        &g_light_config->fade_data[channel];


    fade_data->cycle = 0;
    fade_data->num = 0;
    fade_data->step = 0;


    return ESP_OK;
}


/*
 * Set gamma table.
 */
esp_err_t iot_led_set_gamma_table(
    const uint16_t gamma_table[GAMMA_TABLE_SIZE])
{
    LIGHT_ERROR_CHECK(
        g_gamma_table == NULL,
        ESP_ERR_INVALID_ARG,
        "iot_led_init() must be called first"
    );


    LIGHT_ERROR_CHECK(
        gamma_table == NULL,
        ESP_ERR_INVALID_ARG,
        "gamma_table should not be NULL"
    );


    memcpy(
        g_gamma_table,
        gamma_table,
        GAMMA_TABLE_SIZE *
        sizeof(uint16_t)
    );


    return ESP_OK;
}
