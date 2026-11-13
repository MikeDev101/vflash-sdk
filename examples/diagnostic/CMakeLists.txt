set(diag_gpu_sources
    ${CMAKE_SOURCE_DIR}/src/gpu/gpu.c
    ${CMAKE_SOURCE_DIR}/src/gpu/gpu_software.c
    ${CMAKE_SOURCE_DIR}/src/gpu/gpu_zevio.c
    ${CMAKE_SOURCE_DIR}/src/graphics/renderer.c
)

foreach(mmu IN ITEMS off on)
    set(target vflash_diagnostic_${mmu})
    vflash_add_program(${target} firmware-disc.ld
        main.c
        ${diag_gpu_sources}
    )
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
        "${CMAKE_SOURCE_DIR}/external/SDL2/include"
    )
    target_compile_definitions(${target} PRIVATE DYNAPI_NEEDS_DLOPEN=1)
    if(mmu STREQUAL "on")
        target_sources(${target} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/platform/mmu.c
            ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S
        )
        target_compile_definitions(${target} PRIVATE DIAG_MMU=1)
    endif()
    target_link_libraries(${target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
    vflash_add_disc(diagnostic_${mmu}_disc_image ${target} diagnostic/${mmu})
endforeach()
