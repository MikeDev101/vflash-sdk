set(stress_video_sources
    ${CMAKE_SOURCE_DIR}/src/platform/video_mjp.c
    ${CMAKE_SOURCE_DIR}/src/graphics/picojpeg.c
    ${CMAKE_SOURCE_DIR}/src/platform/cd.c
    ${CMAKE_SOURCE_DIR}/src/platform/fs.c
    ${CMAKE_SOURCE_DIR}/src/platform/clock.c
)

foreach(mmu IN ITEMS off on)
    set(stress_target vflash_stress_${mmu})
    vflash_add_program(${stress_target} firmware-disc.ld
        main.c
        ${stress_video_sources}
    )
    target_compile_definitions(${stress_target} PRIVATE
        VFLASH_DOUBLE_BUFFER=1
    )
    if(mmu STREQUAL "on")
        target_sources(${stress_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
        target_compile_definitions(${stress_target} PRIVATE VFLASH_ENABLE_MMU=1)
    endif()
    target_link_libraries(${stress_target} PRIVATE gcc)
    vflash_add_disc_with_assets(stress_${mmu}_disc_image ${stress_target} stress/${mmu} "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endforeach()
