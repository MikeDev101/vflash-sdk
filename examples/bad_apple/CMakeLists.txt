set(bad_apple_sources
    ${CMAKE_SOURCE_DIR}/src/platform/video_mjp.c
    ${CMAKE_SOURCE_DIR}/src/graphics/picojpeg.c
    ${CMAKE_SOURCE_DIR}/src/platform/cd.c
    ${CMAKE_SOURCE_DIR}/src/platform/fs.c
    ${CMAKE_SOURCE_DIR}/src/platform/clock.c
    ${CMAKE_SOURCE_DIR}/src/platform/loading.c
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/platform/video_mjp.c
    ${CMAKE_SOURCE_DIR}/src/graphics/picojpeg.c
    PROPERTIES COMPILE_OPTIONS "-O3"
)

set(BAD_APPLE_PROFILE "256x192_15fps" CACHE STRING "Bad Apple video profile (e.g. 256x192_15fps, 320x240_10fps, 192x144_24fps, 160x120_30fps)")

set(staged_assets_dir "${CMAKE_CURRENT_BINARY_DIR}/assets")

add_custom_command(
    OUTPUT "${staged_assets_dir}/DISC.MJP"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/stage_bad_apple_assets.py"
        --src-dir "${CMAKE_SOURCE_DIR}/assets/local/bad-apple"
        --dst-dir "${staged_assets_dir}"
        --profile "${BAD_APPLE_PROFILE}"
        --fallback "${CMAKE_SOURCE_DIR}/examples/fmv/assets/disc.mjp"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/stage_bad_apple_assets.py"
    VERBATIM
)

add_custom_target(stage_bad_apple_assets DEPENDS "${staged_assets_dir}/DISC.MJP")

foreach(mmu IN ITEMS off on)
    set(target vflash_bad_apple_${mmu})
    vflash_add_program(${target} firmware-disc.ld
        main.c
        ${bad_apple_sources}
    )
    target_compile_definitions(${target} PRIVATE VFLASH_DOUBLE_BUFFER=1)
    if(mmu STREQUAL "on")
        target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
        target_compile_definitions(${target} PRIVATE VFLASH_ENABLE_MMU=1)
    endif()
    target_link_libraries(${target} PRIVATE gcc)
    add_dependencies(${target} stage_bad_apple_assets)
    vflash_add_disc_with_assets(bad_apple_${mmu}_disc_image ${target} bad_apple/${mmu} "${staged_assets_dir}")
    add_dependencies(bad_apple_${mmu}_disc_image stage_bad_apple_assets)
endforeach()
