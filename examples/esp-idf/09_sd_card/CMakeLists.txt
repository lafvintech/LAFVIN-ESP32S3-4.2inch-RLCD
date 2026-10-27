cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_bsp"
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_display"
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_sdcard"
    "${CMAKE_CURRENT_LIST_DIR}/../components/button_service"
    "${CMAKE_CURRENT_LIST_DIR}/../components/mono_graphics"
)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(09_sd_card)
