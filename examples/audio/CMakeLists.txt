foreach(mmu IN ITEMS off on)
    set(audio_target vflash_audio_${mmu})
    vflash_add_program(${audio_target} firmware-disc.ld audio_demo.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c)
    target_compile_definitions(${audio_target} PRIVATE VFLASH_DOUBLE_BUFFER=1)
    if(mmu STREQUAL "on")
        target_sources(${audio_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
        target_compile_definitions(${audio_target} PRIVATE VFLASH_ENABLE_MMU=1)
    endif()
    vflash_add_disc(audio_${mmu}_disc_image ${audio_target} audio/${mmu})
endforeach()

if(VFLASH_ENABLE_SDL2 AND TARGET SDL2)
    set(sdl2_gpu_sources
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_software.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_zevio.c
        ${CMAKE_SOURCE_DIR}/src/graphics/renderer.c
    )

    foreach(mmu IN ITEMS off on)
        set(sdl2_audio_target vflash_sdl2_audio_${mmu})
        vflash_add_program(${sdl2_audio_target} firmware-disc.ld
            sdl2_audio_demo.c
            ${CMAKE_SOURCE_DIR}/src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${sdl2_audio_target} PRIVATE
            "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${sdl2_audio_target} PRIVATE
            DYNAPI_NEEDS_DLOPEN=1
        )
        if(mmu STREQUAL "on")
            target_sources(${sdl2_audio_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${sdl2_audio_target} PRIVATE VFLASH_ENABLE_MMU=1)
        endif()
        target_link_libraries(${sdl2_audio_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(sdl2_audio_${mmu}_disc_image ${sdl2_audio_target} audio/sdl2/${mmu})
    endforeach()
endif()
