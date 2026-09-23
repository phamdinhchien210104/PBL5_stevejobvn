#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_insights.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_mqtt.h>
#include <esp_rmaker_common_events.h>

#include "app_insights.h"

static const char *TAG = "app_insights";

#define APP_INSIGHTS_LOG_TYPE (ESP_DIAG_LOG_TYPE_ERROR | ESP_DIAG_LOG_TYPE_WARNING | ESP_DIAG_LOG_TYPE_EVENT)

static void rmaker_common_event_handler(void* arg, esp_event_base_t event_base,
                                        int32_t event_id, void* event_data)
{
    if (event_base == RMAKER_COMMON_EVENT) {
        if (event_id == RMAKER_MQTT_EVENT_PUBLISHED && event_data) {
            esp_insights_transport_event_data_t data = {0};
            data.msg_id = *(int *)event_data;
            esp_event_post(INSIGHTS_EVENT, INSIGHTS_EVENT_TRANSPORT_SEND_SUCCESS, &data, sizeof(data), portMAX_DELAY);
        }
    }
}

static int insights_mqtt_data_send(void *data, size_t len)
{
    char topic[128];
    int msg_id = -1;
    if (!data) {
        return 0;
    }
    const char *node_id = esp_insights_get_node_id();
    if (!node_id) {
        node_id = esp_rmaker_get_node_id();
    }
    if (!node_id) {
        ESP_LOGE(TAG, "Cannot send insights data: node_id not set");
        return -1;
    }
    snprintf(topic, sizeof(topic), "node/%s/diagnostics/from-node", node_id);
    esp_err_t err = esp_rmaker_mqtt_publish(topic, data, len, 1, &msg_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to publish insights data over RainMaker MQTT: %s", esp_err_to_name(err));
        return -1;
    }
    return msg_id;
}

esp_err_t app_insights_enable(void)
{
    ESP_LOGI(TAG, "Configuring ESP Insights via RainMaker Shared MQTT Channel...");

    /* 1. Register event handler to bridge RainMaker MQTT publish events to Insights */
    esp_err_t err = esp_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_PUBLISHED,
                                               rmaker_common_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register RMAKER_COMMON_EVENT handler: %s", esp_err_to_name(err));
        return err;
    }

    /* 2. Register custom transport that routes through RainMaker MQTT client */
    static esp_insights_transport_config_t transport_cfg = {
        .callbacks = {
            .init = NULL,
            .deinit = NULL,
            .connect = NULL,
            .disconnect = NULL,
            .data_send = insights_mqtt_data_send,
        },
        .userdata = NULL,
    };
    err = esp_insights_transport_register(&transport_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to register Insights transport: %s", esp_err_to_name(err));
        return err;
    }

    /* 3. Configure telemetry log collection types and enable Insights */
    esp_insights_config_t config = {
        .log_type = APP_INSIGHTS_LOG_TYPE,
        .node_id = esp_rmaker_get_node_id(),
        .alloc_ext_ram = false,
    };
    err = esp_insights_enable(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable Insights: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "ESP Insights enabled successfully (Error/Warning/Event logs + Metrics)");
    return ESP_OK;
}
