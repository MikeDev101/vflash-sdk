if(VFLASH_ENABLE_SDL2 AND TARGET SDL2)
    set(sdl2_gpu_sources
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_software.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_zevio.c
        ${CMAKE_SOURCE_DIR}/src/graphics/renderer.c
    )

    foreach(mmu IN ITEMS off on)
        set(cpp_target vflash_cpp_${mmu})
        vflash_add_program(${cpp_target} firmware-disc.ld
            main.cpp
            ${CMAKE_SOURCE_DIR}/src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${cpp_target} PRIVATE
            "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${cpp_target} PRIVATE
            DYNAPI_NEEDS_DLOPEN=1
        )
        if(mmu STREQUAL "on")
            target_sources(${cpp_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${cpp_target} PRIVATE VFLASH_ENABLE_MMU=1)
        endif()
        target_link_libraries(${cpp_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(cpp_${mmu}_disc_image ${cpp_target} cpp/${mmu})
    endforeach()
endif()
