cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_bsp"
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_display"
    "${CMAKE_CURRENT_LIST_DIR}/../components/button_service"
    "${CMAKE_CURRENT_LIST_DIR}/../components/rlcd_lvgl_port"
)
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.lvgl.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(10_lvgl_ui)
