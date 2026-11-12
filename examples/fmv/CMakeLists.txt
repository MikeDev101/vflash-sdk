set(fmv_video_sources
    ${CMAKE_SOURCE_DIR}/src/platform/video_mjp.c
    ${CMAKE_SOURCE_DIR}/src/graphics/picojpeg.c
    ${CMAKE_SOURCE_DIR}/src/platform/cd.c
    ${CMAKE_SOURCE_DIR}/src/platform/fs.c
    ${CMAKE_SOURCE_DIR}/src/platform/clock.c
)

foreach(mmu IN ITEMS off on)
    set(fmv_target vflash_fmv_${mmu})
    vflash_add_program(${fmv_target} firmware-disc.ld
        main.c
        ${fmv_video_sources}
    )
    if(mmu STREQUAL "on")
        target_sources(${fmv_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
        target_compile_definitions(${fmv_target} PRIVATE VFLASH_ENABLE_MMU=1)
    endif()
    target_link_libraries(${fmv_target} PRIVATE gcc)
    vflash_add_disc_with_assets(fmv_${mmu}_disc_image ${fmv_target} fmv/${mmu} "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endforeach()
