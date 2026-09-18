cmake_minimum_required(VERSION 3.16)

add_compile_options(-fdiagnostics-color=always)

set(COMPONENTS main button app_storage light_driver)

# Add common application components
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components/button
                         ${CMAKE_CURRENT_LIST_DIR}/../components/light_driver
                         ${CMAKE_CURRENT_LIST_DIR}/../components/app_storage
                         )

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(2_light_drivers)
