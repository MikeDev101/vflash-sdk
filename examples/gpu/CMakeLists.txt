set(gpu_sources
    ${CMAKE_SOURCE_DIR}/src/gpu/gpu.c
    ${CMAKE_SOURCE_DIR}/src/gpu/gpu_software.c
    ${CMAKE_SOURCE_DIR}/src/gpu/gpu_zevio.c
    ${CMAKE_SOURCE_DIR}/src/graphics/renderer.c
    ${CMAKE_SOURCE_DIR}/src/graphics/sdl_render.c
)

vflash_add_program(vflash_gpu_software firmware-disc.ld software.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
vflash_add_disc(gpu_software_disc_image vflash_gpu_software gpu/software)

vflash_add_program(vflash_gpu_mmu firmware-disc.ld software.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources}
    ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
target_compile_definitions(vflash_gpu_mmu PRIVATE VFLASH_GPU_MMU=1)
vflash_add_disc(gpu_mmu_disc_image vflash_gpu_mmu gpu/mmu)

vflash_add_program(vflash_gpu_native firmware-disc.ld native.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c)
vflash_add_disc(gpu_native_disc_image vflash_gpu_native gpu/native)

foreach(mmu IN ITEMS off on)
    set(tiled_final 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_tiled_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld tiled.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c)
        target_compile_definitions(${target} PRIVATE GPU_TILED_FINAL=${tiled_final})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_TILED_MMU=1)
        endif()
        vflash_add_disc(gpu_tiled_${mmu}_${view}_disc_image ${target} gpu/tiled/${mmu}/${view})
        math(EXPR tiled_final "${tiled_final}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(final_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_frame_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld integrated.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_FRAME_FINAL=${final_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_FRAME_MMU=1)
        endif()
        vflash_add_disc(gpu_frame_${mmu}_${view}_disc_image ${target} gpu/frame/${mmu}/${view})
        math(EXPR final_view "${final_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(sprite_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_sprite_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld sprite.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_SPRITE_FINAL=${sprite_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_SPRITE_MMU=1)
        endif()
        vflash_add_disc(gpu_sprite_${mmu}_${view}_disc_image ${target} gpu/sprite/${mmu}/${view})
        math(EXPR sprite_view "${sprite_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(transparent_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_transparent_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld transparent.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_TRANSPARENT_FINAL=${transparent_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_TRANSPARENT_MMU=1)
        endif()
        vflash_add_disc(gpu_transparent_${mmu}_${view}_disc_image ${target} gpu/transparent/${mmu}/${view})
        math(EXPR transparent_view "${transparent_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(alpha_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_alpha_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld alpha.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_ALPHA_FINAL=${alpha_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_ALPHA_MMU=1)
        endif()
        vflash_add_disc(gpu_alpha_${mmu}_${view}_disc_image ${target} gpu/alpha/${mmu}/${view})
        math(EXPR alpha_view "${alpha_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(modes_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_modes_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld modes.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_MODES_FINAL=${modes_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_MODES_MMU=1)
        endif()
        vflash_add_disc(gpu_modes_${mmu}_${view}_disc_image ${target} gpu/modes/${mmu}/${view})
        math(EXPR modes_view "${modes_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(renderer_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_renderer_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld renderer_demo.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_RENDERER_FINAL=${renderer_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_RENDERER_MMU=1)
        endif()
        vflash_add_disc(gpu_renderer_${mmu}_${view}_disc_image ${target} gpu/renderer/${mmu}/${view})
        math(EXPR renderer_view "${renderer_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(lifecycle_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_lifecycle_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld lifecycle.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_RENDERER_FINAL=${lifecycle_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_RENDERER_MMU=1)
        endif()
        vflash_add_disc(gpu_lifecycle_${mmu}_${view}_disc_image ${target} gpu/lifecycle/${mmu}/${view})
        math(EXPR lifecycle_view "${lifecycle_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(sdl_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_sdl_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld sdl_demo.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_RENDERER_FINAL=${sdl_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_RENDERER_MMU=1)
        endif()
        vflash_add_disc(gpu_sdl_${mmu}_${view}_disc_image ${target} gpu/sdl/${mmu}/${view})
        math(EXPR sdl_view "${sdl_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(triangle_view 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_triangle_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld triangle.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_TRIANGLE_FINAL=${triangle_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_TRIANGLE_MMU=1)
        endif()
        vflash_add_disc(gpu_triangle_${mmu}_${view}_disc_image ${target} gpu/triangle/${mmu}/${view})

        set(target_depth vflash_gpu_triangle_depth_${mmu}_${view})
        vflash_add_program(${target_depth} firmware-disc.ld triangle_depth.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target_depth} PRIVATE GPU_TRIANGLE_FINAL=${triangle_view})
        if(mmu STREQUAL "on")
            target_sources(${target_depth} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target_depth} PRIVATE GPU_TRIANGLE_MMU=1)
        endif()
        vflash_add_disc(gpu_triangle_depth_${mmu}_${view}_disc_image ${target_depth} gpu/triangle_depth/${mmu}/${view})

        set(target_tex vflash_gpu_triangle_tex_${mmu}_${view})
        vflash_add_program(${target_tex} firmware-disc.ld triangle_tex.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target_tex} PRIVATE GPU_TRIANGLE_FINAL=${triangle_view})
        if(mmu STREQUAL "on")
            target_sources(${target_tex} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target_tex} PRIVATE GPU_TRIANGLE_MMU=1)
        endif()
        vflash_add_disc(gpu_triangle_tex_${mmu}_${view}_disc_image ${target_tex} gpu/triangle_tex/${mmu}/${view})

        set(target_cube vflash_gpu_cube_${mmu}_${view})
        vflash_add_program(${target_cube} firmware-disc.ld cube.c ${CMAKE_SOURCE_DIR}/src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target_cube} PRIVATE GPU_TRIANGLE_FINAL=${triangle_view})
        if(mmu STREQUAL "on")
            target_sources(${target_cube} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${target_cube} PRIVATE GPU_TRIANGLE_MMU=1)
        endif()
        vflash_add_disc(gpu_cube_${mmu}_${view}_disc_image ${target_cube} gpu/cube/${mmu}/${view})

        math(EXPR triangle_view "${triangle_view}+1")
    endforeach()
endforeach()

if(VFLASH_ENABLE_SDL2 AND TARGET SDL2)
    set(sdl2_gpu_sources
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_software.c
        ${CMAKE_SOURCE_DIR}/src/gpu/gpu_zevio.c
        ${CMAKE_SOURCE_DIR}/src/graphics/renderer.c
    )

    foreach(mmu IN ITEMS off on)
        set(sdl2_view 0)
        foreach(view IN ITEMS a b linear)
            set(target vflash_gpu_sdl2_${mmu}_${view})
            vflash_add_program(${target} firmware-disc.ld
                sdl2_testsprite.c
                ${CMAKE_SOURCE_DIR}/external/SDL2/test/testutils.c
                ${CMAKE_SOURCE_DIR}/src/platform/clock.c
                ${sdl2_gpu_sources}
            )
            target_include_directories(${target} PRIVATE
                "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
                "${CMAKE_SOURCE_DIR}/external/SDL2/include"
                "${CMAKE_SOURCE_DIR}/external/SDL2/test"
            )
            target_compile_definitions(${target} PRIVATE
                DYNAPI_NEEDS_DLOPEN=1
                GPU_RENDERER_FINAL=${sdl2_view}
            )
            if(mmu STREQUAL "on")
                target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
                target_compile_definitions(${target} PRIVATE GPU_SDL2_MMU=1)
            endif()
            target_link_libraries(${target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
            vflash_add_disc(gpu_sdl2_${mmu}_${view}_disc_image ${target} gpu/sdl2/${mmu}/${view})

            set(draw_target vflash_gpu_sdl2_draw_${mmu}_${view})
            vflash_add_program(${draw_target} firmware-disc.ld
                sdl2_testdraw.c
                ${CMAKE_SOURCE_DIR}/src/platform/clock.c
                ${sdl2_gpu_sources}
            )
            target_include_directories(${draw_target} PRIVATE
                "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
                "${CMAKE_SOURCE_DIR}/external/SDL2/include"
            )
            target_compile_definitions(${draw_target} PRIVATE
                DYNAPI_NEEDS_DLOPEN=1
                GPU_RENDERER_FINAL=${sdl2_view}
            )
            if(mmu STREQUAL "on")
                target_sources(${draw_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
                target_compile_definitions(${draw_target} PRIVATE GPU_SDL2_MMU=1)
            endif()
            target_link_libraries(${draw_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
            vflash_add_disc(gpu_sdl2_draw_${mmu}_${view}_disc_image ${draw_target} gpu/sdl2_draw/${mmu}/${view})

            math(EXPR sdl2_view "${sdl2_view}+1")
        endforeach()

        set(pen_target vflash_gpu_pen_stress_${mmu})
        vflash_add_program(${pen_target} firmware-disc.ld
            pen_stress.c
            ${CMAKE_SOURCE_DIR}/src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${pen_target} PRIVATE
            "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${pen_target} PRIVATE DYNAPI_NEEDS_DLOPEN=1)
        if(mmu STREQUAL "on")
            target_sources(${pen_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${pen_target} PRIVATE GPU_SDL2_MMU=1)
        endif()
        target_link_libraries(${pen_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(gpu_pen_stress_${mmu}_disc_image ${pen_target} gpu/pen_stress/${mmu})

        set(xform_target vflash_gpu_transform_demo_${mmu})
        vflash_add_program(${xform_target} firmware-disc.ld
            transform_demo.c
            ${CMAKE_SOURCE_DIR}/src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${xform_target} PRIVATE
            "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${xform_target} PRIVATE DYNAPI_NEEDS_DLOPEN=1)
        if(mmu STREQUAL "on")
            target_sources(${xform_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${xform_target} PRIVATE GPU_SDL2_MMU=1)
        endif()
        target_link_libraries(${xform_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(gpu_transform_demo_${mmu}_disc_image ${xform_target} gpu/transform_demo/${mmu})

        set(bench_target vflash_gpu_transform_bench_${mmu})
        vflash_add_program(${bench_target} firmware-disc.ld
            transform_bench.c
            ${CMAKE_SOURCE_DIR}/src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${bench_target} PRIVATE
            "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${bench_target} PRIVATE DYNAPI_NEEDS_DLOPEN=1)
        if(mmu STREQUAL "on")
            target_sources(${bench_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${bench_target} PRIVATE GPU_SDL2_MMU=1)
        endif()
        target_link_libraries(${bench_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(gpu_transform_bench_${mmu}_disc_image ${bench_target} gpu/transform_bench/${mmu})

        set(cmod_target vflash_gpu_color_mod_test_${mmu})
        vflash_add_program(${cmod_target} firmware-disc.ld
            color_mod_test.c
            ${CMAKE_SOURCE_DIR}/src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${cmod_target} PRIVATE
            "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${cmod_target} PRIVATE DYNAPI_NEEDS_DLOPEN=1)
        if(mmu STREQUAL "on")
            target_sources(${cmod_target} PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/mmu.c ${CMAKE_SOURCE_DIR}/src/platform/mmu_arm926.S)
            target_compile_definitions(${cmod_target} PRIVATE GPU_SDL2_MMU=1)
        endif()
        target_link_libraries(${cmod_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(gpu_color_mod_test_${mmu}_disc_image ${cmod_target} gpu/color_mod_test/${mmu})
    endforeach()
endif()
