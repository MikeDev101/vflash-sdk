if(VFLASH_ENABLE_SDL2 AND TARGET SDL2)
    set(sdl2_gpu_sources
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_software.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_zevio.c
        ${CMAKE_SOURCE_DIR}/src/graphics/renderer.c
    )

    foreach(mmu IN ITEMS off on)
        set(suite_target vflash_suite_${mmu})
        vflash_add_program(${suite_target} firmware-disc.ld
            main.c
            ${CMAKE_SOURCE_DIR}/src/platform/clock.c
            ${CMAKE_SOURCE_DIR}/src/platform/mmu.c
            ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S
            ${CMAKE_SOURCE_DIR}/src/platform/video_mjp.c
            ${CMAKE_SOURCE_DIR}/src/graphics/picojpeg.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${suite_target} PRIVATE
            "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${suite_target} PRIVATE
            DYNAPI_NEEDS_DLOPEN=1
            VFLASH_DOUBLE_BUFFER=1
        )
        if(mmu STREQUAL "on")
            target_compile_definitions(${suite_target} PRIVATE VFLASH_ENABLE_MMU=1)
        endif()
        target_link_libraries(${suite_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc_with_assets(suite_${mmu}_disc_image ${suite_target} suite/${mmu} "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    endforeach()
endif()
