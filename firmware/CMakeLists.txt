cmake_minimum_required(VERSION 3.16)

set(CDP_DEFAULT_FIRMWARE_VERSION "3.1.0-phase3-ux-dev")
if(DEFINED CDP_FIRMWARE_VERSION_OVERRIDE)
    set(PROJECT_VER "${CDP_FIRMWARE_VERSION_OVERRIDE}")
else()
    set(PROJECT_VER "${CDP_DEFAULT_FIRMWARE_VERSION}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(robot_attendance)
