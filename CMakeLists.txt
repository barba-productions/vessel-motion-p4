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
project(vessel_motion_p4 VERSION 1.0.0)
