# Each executable compiles its own SDK sources so build-time video and panic
# options cannot leak between programs. Target names and ELF contracts are stable.
function(vflash_add_program target script)
    add_executable(${target} startup/startup.S startup/panic_entry.S
        src/runtime/panic.c src/runtime/panic_render.c
        src/platform/video.c src/platform/input.c src/graphics/draw.c src/graphics/font.c ${ARGN})
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/examples/common")
    set_target_properties(${target} PROPERTIES SUFFIX ".elf" C_STANDARD 11 C_STANDARD_REQUIRED YES)
    set(arch_flags -mcpu=arm926ej-s -marm -mfloat-abi=soft -mlittle-endian -mabi=aapcs -fno-short-enums)
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
