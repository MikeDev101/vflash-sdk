cmake_minimum_required(VERSION 3.20)
project(vflash_sdk VERSION 0.1.0 LANGUAGES C ASM)

if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake (GNU Arm required)")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(tools_dir "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(cmake/VFlashTargets.cmake)
include(cmake/DiscImage.cmake)

set(hello_sources examples/hello/main.c examples/common/demo.c)
vflash_add_program(vflash_probe flashem-direct.ld ${hello_sources})
vflash_add_program(vflash_disc_probe firmware-disc.ld ${hello_sources})
vflash_add_disc(disc_image vflash_disc_probe ".")
add_custom_target(boot_image ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/BOOT.BIN")

vflash_add_program(vflash_animation_probe firmware-disc.ld
    examples/animation/main.c examples/animation/animation.c src/platform/clock.c)
target_compile_definitions(vflash_animation_probe PRIVATE VFLASH_DOUBLE_BUFFER=1)
# Compiler integer-division helpers only; no libc or operating-system runtime.
target_link_libraries(vflash_animation_probe PRIVATE gcc)
vflash_add_disc(animation_disc_image vflash_animation_probe animation)

# Index order is part of the diagnostic assembly's PANIC_TEST_MODE contract.
set(panic_modes manual undefined stack nested swi thumb restore system stack-undefined thumb-swi)
set(panic_mode_index 0)
foreach(mode IN LISTS panic_modes)
    vflash_add_program(vflash_panic_${mode} firmware-disc.ld
        examples/panic/main.c examples/panic/triggers.S examples/common/demo.c src/platform/clock.c)
    target_compile_definitions(vflash_panic_${mode} PRIVATE PANIC_TEST_MODE=${panic_mode_index})
    if(mode STREQUAL "nested")
        target_compile_definitions(vflash_panic_${mode} PRIVATE VFLASH_PANIC_INJECT_NESTED=1)
    endif()
    vflash_add_disc(panic_${mode}_disc_image vflash_panic_${mode} panic/${mode})
    math(EXPR panic_mode_index "${panic_mode_index} + 1")
endforeach()

enable_testing()
add_test(NAME host_tools COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)
add_test(NAME elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_probe>" --readelf "${CMAKE_READELF}")
add_test(NAME disc_elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_disc_probe>" --readelf "${CMAKE_READELF}")
add_test(NAME animation_elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_animation_probe>" --readelf "${CMAKE_READELF}")

# Optional MMU bring-up programs; normal targets never link/enable this driver.
foreach(mode IN ITEMS identity undefined)
    vflash_add_program(vflash_mmu_${mode} firmware-disc.ld examples/mmu/main.c
        examples/common/demo.c src/platform/clock.c src/platform/mmu.c src/platform/mmu_arm926.S)
    if(mode STREQUAL "undefined")
        target_compile_definitions(vflash_mmu_${mode} PRIVATE VFLASH_MMU_UDF=1)
    endif()
    vflash_add_disc(mmu_${mode}_disc_image vflash_mmu_${mode} mmu/${mode})
endforeach()

vflash_add_program(vflash_mmu_animation firmware-disc.ld examples/animation/main.c
    examples/animation/animation.c src/platform/clock.c src/platform/mmu.c src/platform/mmu_arm926.S)
target_compile_definitions(vflash_mmu_animation PRIVATE VFLASH_DOUBLE_BUFFER=1 VFLASH_ENABLE_MMU=1)
target_link_libraries(vflash_mmu_animation PRIVATE gcc)
vflash_add_disc(mmu_animation_disc_image vflash_mmu_animation mmu/animation)
