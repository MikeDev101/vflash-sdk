# VFlash CMake package configuration file
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED VFLASH_SDK_DIR)
    get_filename_component(VFLASH_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(VFLASH_FOUND TRUE)
set(VFLASH_INCLUDE_DIRS "${VFLASH_SDK_DIR}/include")

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

include("${CMAKE_CURRENT_LIST_DIR}/VFlashTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/DiscImage.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SDL2.cmake")
