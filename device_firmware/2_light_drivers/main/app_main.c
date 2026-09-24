/*
 * ESP32-C3 Smart Light Project
 * Section 6.5 Practice: Adding Drivers to Smart Light Project
 * - Button Driver (GPIO 9 Boot button, BUTTON_PRESS_UP)
 * - LED Dimming Driver (WS2812B NeoPixel 8-bit mapping)
 * - NVS Persistence (LIGHT_STATUS_STORE_KEY)
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

#include "app_storage.h"
#include "light_driver.h"
#include "app_priv.h"

static const char *TAG = "app_main";

void app_main(void)
{
    /* Tối ưu hóa mức độ log (Observability & Signal-to-Noise Ratio):
     * Ẩn các log debug API chi tiết của driver để làm nổi bật các sự kiện nhấn nút và trạng thái đèn */
    esp_log_level_set("light_driver", ESP_LOG_WARN);

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "  PBL5 Smart Light: Section 6.5 Adding Drivers Practice   ");
    ESP_LOGI(TAG, "==========================================================");

    /**
     * @brief 1. Khởi tạo NVS Flash lưu trữ trạng thái (Section 6.5.2)
     */
    ESP_LOGI(TAG, "Khởi tạo NVS Flash lưu trữ trạng thái...");
    ESP_ERROR_CHECK(app_storage_init());

    /**
     * @brief 2. Khởi tạo Button Driver & Light Driver (Section 6.5.1 + 6.5.2)
     */
    ESP_LOGI(TAG, "Khởi tạo Tầng Driver (Button + Light Driver + WS2812B)...");
    app_driver_init();

    // In thông tin trạng thái khôi phục từ Flash
    ESP_LOGI(TAG, "----------------------------------------------------------");
    ESP_LOGI(TAG, "Trạng thái khôi phục từ Flash NVS:");
    ESP_LOGI(TAG, " - Công tắc đèn : %s", light_driver_get_switch() ? "BẬT (ON)" : "TẮT (OFF)");
    ESP_LOGI(TAG, " - Độ sáng      : %d%%", light_driver_get_brightness());
    ESP_LOGI(TAG, " - Chế độ sáng  : %d", light_driver_get_mode());
    ESP_LOGI(TAG, "----------------------------------------------------------");
    ESP_LOGI(TAG, "Hướng dẫn kiểm tra phần cứng:");
    ESP_LOGI(TAG, " 1. Nhấn nút Boot (GPIO9): Nhấn 1 lần để Bật/Tắt đèn (Chốt trạng thái).");
    ESP_LOGI(TAG, " 2. Nhấn đúp nút Boot: Đổi màu sắc (8 màu RGB) và lưu vào Flash.");
    ESP_LOGI(TAG, " 3. Nhấn giữ nút Boot: Dimming vô cấp đảo chiều (Tự dừng tại 100%% hoặc 5%%, nhả tay chốt độ sáng).");
    ESP_LOGI(TAG, " 4. Rút nguồn và cắm lại: Kiểm tra đèn khôi phục đúng trạng thái trước khi tắt.");
    ESP_LOGI(TAG, "==========================================================");

    int loop_cnt = 0;
    while (1) {
        ESP_LOGI(TAG, "[Giám sát #%02d] Trạng thái: %s | Độ sáng: %d%% | Free Heap: %lu bytes",
                 ++loop_cnt,
                 light_driver_get_switch() ? "BẬT (ON)" : "TẮT (OFF)",
                 light_driver_get_brightness(),
                 (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
