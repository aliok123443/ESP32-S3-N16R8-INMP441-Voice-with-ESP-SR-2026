cmake_minimum_required(VERSION 3.16)

# Requires ESP-IDF v5.3.4. Set IDF_PATH by running the ESP-IDF export script
# before invoking idf.py. ESP-SR is downloaded by the Component Manager.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(voice_lights)
