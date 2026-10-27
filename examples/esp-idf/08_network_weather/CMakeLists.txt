cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_bsp"
    "${CMAKE_CURRENT_LIST_DIR}/../components/board_display"
    "${CMAKE_CURRENT_LIST_DIR}/../components/button_service"
    "${CMAKE_CURRENT_LIST_DIR}/../components/mono_graphics"
    "${CMAKE_CURRENT_LIST_DIR}/../components/pcf85063"
    "${CMAKE_CURRENT_LIST_DIR}/../components/shtc3"
    "${CMAKE_CURRENT_LIST_DIR}/../components/weather_client"
    "${CMAKE_CURRENT_LIST_DIR}/../components/wifi_service"
)
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(08_network_weather)
