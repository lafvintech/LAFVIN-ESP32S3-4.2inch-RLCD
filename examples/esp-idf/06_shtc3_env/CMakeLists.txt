cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_bsp"
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_display"
    "${CMAKE_CURRENT_LIST_DIR}/../components/mono_graphics"
    "${CMAKE_CURRENT_LIST_DIR}/../components/shtc3"
)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(06_shtc3_env)
