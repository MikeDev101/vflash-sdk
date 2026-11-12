foreach(mmu IN ITEMS off on)
    set(input_target vflash_input_${mmu})
    vflash_add_program(${input_target} firmware-disc.ld controller_demo.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c)
    target_compile_definitions(${input_target} PRIVATE VFLASH_DOUBLE_BUFFER=1)
    if(mmu STREQUAL "on")
        target_sources(${input_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
        target_compile_definitions(${input_target} PRIVATE VFLASH_ENABLE_MMU=1)
    endif()
    vflash_add_disc(input_${mmu}_disc_image ${input_target} input/${mmu})
endforeach()

if(VFLASH_ENABLE_SDL2 AND TARGET SDL2)
    set(sdl2_gpu_sources
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_software.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_zevio.c
        ${CMAKE_SOURCE_DIR}/src/graphics/renderer.c
    )

    foreach(mmu IN ITEMS off on)
        set(sdl2_input_target vflash_sdl2_input_${mmu})
        vflash_add_program(${sdl2_input_target} firmware-disc.ld
            sdl2_controller_demo.c
            ${CMAKE_SOURCE_DIR}/src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${sdl2_input_target} PRIVATE
            "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${sdl2_input_target} PRIVATE
            DYNAPI_NEEDS_DLOPEN=1
        )
        if(mmu STREQUAL "on")
            target_sources(${sdl2_input_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${sdl2_input_target} PRIVATE VFLASH_ENABLE_MMU=1)
        endif()
        target_link_libraries(${sdl2_input_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(sdl2_input_${mmu}_disc_image ${sdl2_input_target} input/sdl2/${mmu})
    endforeach()
endif()
