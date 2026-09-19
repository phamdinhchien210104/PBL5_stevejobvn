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

#ifndef __APP_PRIVATE_H__
#define __APP_PRIVATE_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LIGHT_MODE_NORMAL = 0,    /**< Chế độ sáng bình thường (độ sáng ổn định 100%) */
    LIGHT_MODE_DIMMING = 1,   /**< Chế độ dimming (tăng giảm độ sáng tuần hoàn / breathing) */
} app_light_mode_t;

/**
 * @brief Khởi tạo button driver và light driver
 */
void app_driver_init(void);

/**
 * @brief Bật hoặc tắt đèn
 * 
 * @param state true: Bật, false: Tắt
 * @return int ESP_OK
 */
int app_driver_set_state(bool state);

/**
 * @brief Lấy trạng thái Bật/Tắt hiện tại của đèn
 * 
 * @return true Đèn đang Bật
 * @return false Đèn đang Tắt
 */
bool app_driver_get_state(void);

/**
 * @brief Chuyển đổi giữa chế độ sáng bình thường và dimming (dành cho Double Click)
 */
void app_driver_toggle_mode(void);

/**
 * @brief Đổi sang màu kế tiếp trong bảng màu (dành cho Single Click)
 */
void app_driver_next_color(void);

/**
 * @brief Lấy chế độ sáng hiện tại (Normal hoặc Dimming)
 */
app_light_mode_t app_driver_get_mode(void);

#ifdef __cplusplus
}
#endif

#endif /**< __APP_PRIVATE_H__ */
