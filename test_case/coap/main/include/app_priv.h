// Copyright 2026 PBL5 Stevejobvn Team
#ifndef __APP_PRIVATE_H__
#define __APP_PRIVATE_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_driver_init(void);
int app_driver_set_state(bool state);
bool app_driver_get_state(void);
int app_driver_toggle_state(void);
int app_driver_set_color(uint8_t red, uint8_t green, uint8_t blue);
uint8_t app_driver_get_brightness(void);
int app_driver_set_brightness(uint8_t brightness);
int app_driver_adjust_brightness(int delta);
uint8_t app_driver_get_color_index(void);
const char* app_driver_get_color_name(void);
void app_driver_get_rgb(uint8_t *r, uint8_t *g, uint8_t *b);
int app_driver_next_color(void);

#ifdef __cplusplus
}
#endif

#endif /**< __APP_PRIVATE_H__ */
