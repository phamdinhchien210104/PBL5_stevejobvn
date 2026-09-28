# Cấu hình tự động nhận diện target ESP32-S3 hoặc ESP32-C3
if(CONFIG_IDF_TARGET_ESP32S3)
    set(DEVELOPMENT_BOARD "board_esp32s3_devkitc.h")
else()
    set(DEVELOPMENT_BOARD "board_esp32c3_devkitc.h")
endif()

idf_component_register(SRCS "app_main.c" "app_driver.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash light_driver button app_storage coap)

idf_build_set_property(COMPILE_DEFINITIONS "-DDEVELOPMENT_BOARD=\"${DEVELOPMENT_BOARD}\"" APPEND)
