cmake_minimum_required(VERSION 3.20)
project(vflash_sdk VERSION 0.1.0 LANGUAGES C CXX ASM)

if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "VFlash" OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-vflash.cmake (GNU Arm required)")
endif()

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(tools_dir "${CMAKE_CURRENT_SOURCE_DIR}/tools")

include(cmake/VFlashTargets.cmake)
include(cmake/DiscImage.cmake)
include(cmake/SDL2.cmake)

enable_testing()
add_test(NAME host_tools COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)

add_subdirectory(examples)

add_test(NAME elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_probe>" --readelf "${CMAKE_READELF}")
add_test(NAME disc_elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_disc_probe>" --readelf "${CMAKE_READELF}")
add_test(NAME animation_elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_animation_probe>" --readelf "${CMAKE_READELF}")
