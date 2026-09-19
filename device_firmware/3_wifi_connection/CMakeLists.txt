# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

add_compile_options(-fdiagnostics-color=always)

# Prune unneeded ESP-IDF components to prevent GCC 15 ICE crash on esp_lcd and accelerate build
set(COMPONENTS main button app_storage light_driver)

# Add common application components
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components/light_driver
                        ${CMAKE_CURRENT_LIST_DIR}/../components/button
                        ${CMAKE_CURRENT_LIST_DIR}/../components/app_storage
                        )

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(3_wifi_connection)
