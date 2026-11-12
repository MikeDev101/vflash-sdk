cmake_minimum_required(VERSION 3.20)
project(vflash_duck_test VERSION 1.0.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(VFlash REQUIRED)

vflash_add_program(duck_test "firmware-disc.ld"
    src/main.cpp
    src/game.cpp
)

target_link_libraries(duck_test PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)

vflash_add_disc_with_assets(duck_test_disc duck_test "disc" "${CMAKE_CURRENT_SOURCE_DIR}/assets")
