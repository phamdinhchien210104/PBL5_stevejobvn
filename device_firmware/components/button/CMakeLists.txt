set(srcs "button_gpio.c" "iot_button.c" "button_adc_stub.c")

idf_component_register(SRCS ${srcs}
                        INCLUDE_DIRS include
                        PRIV_REQUIRES esp_driver_gpio esp_timer)
