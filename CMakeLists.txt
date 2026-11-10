cmake_minimum_required(VERSION 3.20)
project(vflash_sdk VERSION 0.1.0 LANGUAGES C ASM)

if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake (GNU Arm required)")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(tools_dir "${CMAKE_CURRENT_SOURCE_DIR}/tools")
function(add_probe target script)
    set(probe_sources src/main.c src/demo.c)
    if(ARGN)
        set(probe_sources ${ARGN})
    endif()
    add_executable(${target} startup/startup.S src/video.c src/draw.c src/font.c ${probe_sources})
    set_target_properties(${target} PROPERTIES SUFFIX ".elf" C_STANDARD 11 C_STANDARD_REQUIRED YES)
    set(arch_flags -mcpu=arm926ej-s -marm -mfloat-abi=soft -mlittle-endian -mabi=aapcs)
    target_compile_options(${target} PRIVATE ${arch_flags} -Os -g3 -ffreestanding -fno-builtin
        -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
        -Wall -Wextra -Werror)
    set(linker_script "${CMAKE_CURRENT_SOURCE_DIR}/startup/${script}")
    target_link_options(${target} PRIVATE ${arch_flags} -nostdlib -nostartfiles -static
        "-T${linker_script}" -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${target}.map" -Wl,--no-undefined)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${linker_script}")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin"
        COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/capture_tool.py" "${CMAKE_CURRENT_BINARY_DIR}/${target}.dis"
            "${CMAKE_OBJDUMP}" -d -S "$<TARGET_FILE:${target}>"
        COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/capture_tool.py" "${CMAKE_CURRENT_BINARY_DIR}/${target}.readelf.txt"
            "${CMAKE_READELF}" -h -l -S -A -s "$<TARGET_FILE:${target}>"
        COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py" "$<TARGET_FILE:${target}>"
            --readelf "${CMAKE_READELF}"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin" "${CMAKE_CURRENT_BINARY_DIR}/${target}.dis"
            "${CMAKE_CURRENT_BINARY_DIR}/${target}.readelf.txt" "${CMAKE_CURRENT_BINARY_DIR}/${target}.map"
        VERBATIM)
endfunction()

add_probe(vflash_probe flashem-direct.ld)
add_probe(vflash_disc_probe firmware-disc.ld)
add_probe(vflash_animation_probe firmware-disc.ld src/animation_main.c src/animation.c src/clock.c)
target_compile_definitions(vflash_animation_probe PRIVATE VFLASH_DOUBLE_BUFFER=1)
# Compiler integer-division helpers only; no libc or operating-system runtime.
target_link_libraries(vflash_animation_probe PRIVATE gcc)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/BOOT.BIN"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/BOOT.BIN"
    COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/pack_boot.py" "$<TARGET_FILE:vflash_disc_probe>"
        "${CMAKE_CURRENT_BINARY_DIR}/BOOT.BIN" --verbose
    DEPENDS vflash_disc_probe "${tools_dir}/pack_boot.py" "${tools_dir}/elf_image.py" VERBATIM)
add_custom_target(boot_image ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/BOOT.BIN")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/homebrew.iso"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/homebrew.iso"
    COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/make_iso.py"
        "${CMAKE_CURRENT_BINARY_DIR}/BOOT.BIN" "${CMAKE_CURRENT_BINARY_DIR}/homebrew.iso"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/BOOT.BIN" "${tools_dir}/make_iso.py"
        "${tools_dir}/inspect_boot.py" "${tools_dir}/elf_image.py" VERBATIM)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/homebrew.cue" "${CMAKE_CURRENT_BINARY_DIR}/homebrew.bin"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/homebrew.cue" "${CMAKE_CURRENT_BINARY_DIR}/homebrew.bin"
    COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/raw_cd.py"
        "${CMAKE_CURRENT_BINARY_DIR}/homebrew.iso" "${CMAKE_CURRENT_BINARY_DIR}/homebrew.cue"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/homebrew.iso" "${tools_dir}/raw_cd.py" VERBATIM)
add_custom_target(disc_image DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/homebrew.cue" "${CMAKE_CURRENT_BINARY_DIR}/homebrew.bin")

set(animation_dir "${CMAKE_CURRENT_BINARY_DIR}/animation")
add_custom_command(OUTPUT "${animation_dir}/BOOT.BIN"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${animation_dir}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${animation_dir}/BOOT.BIN"
    COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/pack_boot.py" "$<TARGET_FILE:vflash_animation_probe>"
        "${animation_dir}/BOOT.BIN" --verbose
    DEPENDS vflash_animation_probe "${tools_dir}/pack_boot.py" "${tools_dir}/elf_image.py" VERBATIM)
add_custom_command(OUTPUT "${animation_dir}/homebrew.iso"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${animation_dir}/homebrew.iso"
    COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/make_iso.py" "${animation_dir}/BOOT.BIN" "${animation_dir}/homebrew.iso"
    DEPENDS "${animation_dir}/BOOT.BIN" "${tools_dir}/make_iso.py" "${tools_dir}/inspect_boot.py" VERBATIM)
add_custom_command(OUTPUT "${animation_dir}/homebrew.cue" "${animation_dir}/homebrew.bin"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${animation_dir}/homebrew.cue" "${animation_dir}/homebrew.bin"
    COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/raw_cd.py" "${animation_dir}/homebrew.iso" "${animation_dir}/homebrew.cue"
    DEPENDS "${animation_dir}/homebrew.iso" "${tools_dir}/raw_cd.py" VERBATIM)
add_custom_target(animation_disc_image DEPENDS "${animation_dir}/homebrew.cue" "${animation_dir}/homebrew.bin")

enable_testing()
add_test(NAME host_tools COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)
add_test(NAME elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_probe>" --readelf "${CMAKE_READELF}")
add_test(NAME disc_elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_disc_probe>" --readelf "${CMAKE_READELF}")
add_test(NAME animation_elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_animation_probe>" --readelf "${CMAKE_READELF}")
