set(srcs "app_main.c"
         "app_driver.c")
set(include_dirs "include")

if(CONFIG_IDF_TARGET_ESP32S3)
    set(DEVELOPMENT_BOARD "board_esp32s3_devkitc.h")
elseif(CONFIG_IDF_TARGET_ESP32C3)
    set(DEVELOPMENT_BOARD "board_esp32c3_devkitc.h")
else()
    set(DEVELOPMENT_BOARD "board_esp32c3_devkitc.h")
endif()

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       PRIV_REQUIRES button app_storage light_driver esp_wifi esp_event esp_netif nvs_flash bt)

idf_build_set_property(COMPILE_DEFINITIONS "-DDEVELOPMENT_BOARD=\"${DEVELOPMENT_BOARD}\"" APPEND)
