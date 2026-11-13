if(NOT DEFINED VFLASH_SDK_DIR)
    get_filename_component(VFLASH_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

# Each executable compiles its own SDK sources so build-time video and panic
# options cannot leak between programs. Target names and ELF contracts are stable.
function(vflash_add_program target script)
    add_executable(${target}
        "${VFLASH_SDK_DIR}/startup/startup.S"
        "${VFLASH_SDK_DIR}/startup/panic_entry.S"
        "${VFLASH_SDK_DIR}/src/runtime/panic.c"
        "${VFLASH_SDK_DIR}/src/runtime/panic_render.c"
        "${VFLASH_SDK_DIR}/src/runtime/alloc.c"
        "${VFLASH_SDK_DIR}/src/runtime/cxxabi.cpp"
        "${VFLASH_SDK_DIR}/src/platform/clock.c"
        "${VFLASH_SDK_DIR}/src/platform/cache_arm926.S"
        "${VFLASH_SDK_DIR}/src/platform/video.c"
        "${VFLASH_SDK_DIR}/src/platform/input.c"
        "${VFLASH_SDK_DIR}/src/platform/audio.c"
        "${VFLASH_SDK_DIR}/src/graphics/draw.c"
        "${VFLASH_SDK_DIR}/src/graphics/font.c"
        "${VFLASH_SDK_DIR}/src/platform/loading.c"
        ${ARGN})
    target_include_directories(${target} PRIVATE
        "${VFLASH_SDK_DIR}/include")
    if(EXISTS "${VFLASH_SDK_DIR}/examples/common")
        target_include_directories(${target} PRIVATE "${VFLASH_SDK_DIR}/examples/common")
    endif()
    set_target_properties(${target} PROPERTIES
        SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        C_STANDARD 11 C_STANDARD_REQUIRED YES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
    set(arch_flags -mcpu=arm926ej-s -marm -mfloat-abi=soft -mlittle-endian -mabi=aapcs -fno-short-enums)
    if(NOT DEFINED VFLASH_OPT_LEVEL)
        set(VFLASH_OPT_LEVEL "-O3" CACHE STRING "Compiler optimization level (-O1, -O2, -O3, -Os)")
    endif()
    target_compile_options(${target} PRIVATE ${arch_flags} ${VFLASH_OPT_LEVEL} -g3 -ffreestanding -fno-builtin
        -fno-delete-null-pointer-checks -Wno-array-bounds
        -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
        -Wall -Wextra -Werror
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
    set(linker_script "${VFLASH_SDK_DIR}/startup/${script}")
    target_link_options(${target} PRIVATE ${arch_flags} -nostdlib -nostartfiles -static
        "-T${linker_script}" -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack
        "-Wl,-Map,${CMAKE_BINARY_DIR}/${target}.map" -Wl,--no-undefined
        -lgcc)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${linker_script}")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${CMAKE_BINARY_DIR}/${target}.bin"
        COMMAND "${Python3_EXECUTABLE}" "${VFLASH_SDK_DIR}/tools/verify_elf.py" "$<TARGET_FILE:${target}>"
            --readelf "${CMAKE_READELF}"
            --objdump "${CMAKE_OBJDUMP}"
            --dis-out "${CMAKE_BINARY_DIR}/${target}.dis"
            --readelf-out "${CMAKE_BINARY_DIR}/${target}.readelf.txt"
        BYPRODUCTS "${CMAKE_BINARY_DIR}/${target}.bin" "${CMAKE_BINARY_DIR}/${target}.dis"
            "${CMAKE_BINARY_DIR}/${target}.readelf.txt" "${CMAKE_BINARY_DIR}/${target}.map"
        VERBATIM)
endfunction()
