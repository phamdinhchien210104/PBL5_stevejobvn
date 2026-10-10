# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

add_compile_options(-fdiagnostics-color=always)

# Kế thừa các components dùng chung chuẩn hóa của dự án PBL5
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../device_firmware/components)

# Khiên bảo vệ chống crash trình biên dịch GCC 15 ICE trên ESP-IDF v6.0.2
set(COMPONENTS main button app_storage light_driver nvs_flash esp_wifi esp_event esp_netif esp_https_server esp_http_server)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(https_server)
