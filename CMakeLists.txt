# Minimal ESP-IDF project for the Waveshare ESP32-P4-NANO board.
# For more information about build system's CMakeLists.txt files,
# see https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/build-system.html
cmake_minimum_required(VERSION 3.16)

# The esp_hosted component ships example trees whose paths exceed the Windows
# 260-character limit when extracted to the default Component Manager cache.
# Use a short cache path so extraction succeeds regardless of how the build
# is launched (terminal or the VS Code ESP-IDF extension).
if(NOT DEFINED ENV{IDF_COMPONENT_CACHE_PATH})
    set(ENV{IDF_COMPONENT_CACHE_PATH} "C:/cm")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
if(NOT DEFINED WIRELESS_ALLOWED)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/main/globals.h" WIRELESS_LINE
         REGEX "^#define WIRELESS_ALLOWED [01]$")
    string(REGEX REPLACE ".* ([01])$" "\\1" WIRELESS_ALLOWED "${WIRELESS_LINE}")
endif()
if(NOT WIRELESS_ALLOWED MATCHES "^[01]$")
    message(FATAL_ERROR "WIRELESS_ALLOWED must be 0 or 1")
endif()
set(ENV{VESSEL_WIRELESS_ALLOWED} "${WIRELESS_ALLOWED}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/main/globals.h")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/main/globals.h" VERSION_LINE
     REGEX "^#define FIRMWARE_VERSION ")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" PROJECT_VER "${VERSION_LINE}")
project(vessel_motion_p4)
