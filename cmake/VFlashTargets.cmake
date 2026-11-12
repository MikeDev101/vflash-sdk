# Each executable compiles its own SDK sources so build-time video and panic
# options cannot leak between programs. Target names and ELF contracts are stable.
function(vflash_add_program target script)
    add_executable(${target}
        "${CMAKE_SOURCE_DIR}/startup/startup.S"
        "${CMAKE_SOURCE_DIR}/startup/panic_entry.S"
        "${CMAKE_SOURCE_DIR}/src/runtime/panic.c"
        "${CMAKE_SOURCE_DIR}/src/runtime/panic_render.c"
        "${CMAKE_SOURCE_DIR}/src/runtime/alloc.c"
        "${CMAKE_SOURCE_DIR}/src/runtime/cxxabi.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/video.c"
        "${CMAKE_SOURCE_DIR}/src/platform/input.c"
        "${CMAKE_SOURCE_DIR}/src/platform/audio.c"
        "${CMAKE_SOURCE_DIR}/src/graphics/draw.c"
        "${CMAKE_SOURCE_DIR}/src/graphics/font.c"
        ${ARGN})
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/examples/common")
    set_target_properties(${target} PROPERTIES
        SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        C_STANDARD 11 C_STANDARD_REQUIRED YES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
    set(arch_flags -mcpu=arm926ej-s -marm -mfloat-abi=soft -mlittle-endian -mabi=aapcs -fno-short-enums)
    target_compile_options(${target} PRIVATE ${arch_flags} -Os -g3 -ffreestanding -fno-builtin
        -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
        -Wall -Wextra -Werror
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
    set(linker_script "${CMAKE_SOURCE_DIR}/startup/${script}")
    target_link_options(${target} PRIVATE ${arch_flags} -nostdlib -nostartfiles -static
        "-T${linker_script}" -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack
        "-Wl,-Map,${CMAKE_BINARY_DIR}/${target}.map" -Wl,--no-undefined)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${linker_script}")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${CMAKE_BINARY_DIR}/${target}.bin"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/verify_elf.py" "$<TARGET_FILE:${target}>"
            --readelf "${CMAKE_READELF}"
            --objdump "${CMAKE_OBJDUMP}"
            --dis-out "${CMAKE_BINARY_DIR}/${target}.dis"
            --readelf-out "${CMAKE_BINARY_DIR}/${target}.readelf.txt"
        BYPRODUCTS "${CMAKE_BINARY_DIR}/${target}.bin" "${CMAKE_BINARY_DIR}/${target}.dis"
            "${CMAKE_BINARY_DIR}/${target}.readelf.txt" "${CMAKE_BINARY_DIR}/${target}.map"
        VERBATIM)
endfunction()
