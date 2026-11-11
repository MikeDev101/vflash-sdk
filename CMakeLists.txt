cmake_minimum_required(VERSION 3.20)
project(vflash_sdk VERSION 0.1.0 LANGUAGES C ASM)

if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake (GNU Arm required)")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(tools_dir "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(cmake/VFlashTargets.cmake)
include(cmake/DiscImage.cmake)

set(hello_sources examples/hello/main.c examples/common/demo.c)
vflash_add_program(vflash_probe flashem-direct.ld ${hello_sources})
vflash_add_program(vflash_disc_probe firmware-disc.ld ${hello_sources})
vflash_add_disc(disc_image vflash_disc_probe ".")
add_custom_target(boot_image ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/BOOT.BIN")

vflash_add_program(vflash_animation_probe firmware-disc.ld
    examples/animation/main.c examples/animation/animation.c src/platform/clock.c)
target_compile_definitions(vflash_animation_probe PRIVATE VFLASH_DOUBLE_BUFFER=1)
# Compiler integer-division helpers only; no libc or operating-system runtime.
target_link_libraries(vflash_animation_probe PRIVATE gcc)
vflash_add_disc(animation_disc_image vflash_animation_probe animation)

# Index order is part of the diagnostic assembly's PANIC_TEST_MODE contract.
set(panic_modes manual undefined stack nested swi thumb restore system stack-undefined thumb-swi)
set(panic_mode_index 0)
foreach(mode IN LISTS panic_modes)
    vflash_add_program(vflash_panic_${mode} firmware-disc.ld
        examples/panic/main.c examples/panic/triggers.S examples/common/demo.c src/platform/clock.c)
    target_compile_definitions(vflash_panic_${mode} PRIVATE PANIC_TEST_MODE=${panic_mode_index})
    if(mode STREQUAL "nested")
        target_compile_definitions(vflash_panic_${mode} PRIVATE VFLASH_PANIC_INJECT_NESTED=1)
    endif()
    vflash_add_disc(panic_${mode}_disc_image vflash_panic_${mode} panic/${mode})
    math(EXPR panic_mode_index "${panic_mode_index} + 1")
endforeach()

enable_testing()
add_test(NAME host_tools COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)
add_test(NAME elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_probe>" --readelf "${CMAKE_READELF}")
add_test(NAME disc_elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_disc_probe>" --readelf "${CMAKE_READELF}")
add_test(NAME animation_elf_contract COMMAND "${Python3_EXECUTABLE}" "${tools_dir}/verify_elf.py"
    "$<TARGET_FILE:vflash_animation_probe>" --readelf "${CMAKE_READELF}")

# Optional MMU bring-up programs; normal targets never link/enable this driver.
foreach(mode IN ITEMS identity undefined)
    vflash_add_program(vflash_mmu_${mode} firmware-disc.ld examples/mmu/main.c
        examples/common/demo.c src/platform/clock.c src/platform/mmu.c src/platform/mmu_arm926.S)
    if(mode STREQUAL "undefined")
        target_compile_definitions(vflash_mmu_${mode} PRIVATE VFLASH_MMU_UDF=1)
    endif()
    vflash_add_disc(mmu_${mode}_disc_image vflash_mmu_${mode} mmu/${mode})
endforeach()

vflash_add_program(vflash_mmu_animation firmware-disc.ld examples/animation/main.c
    examples/animation/animation.c src/platform/clock.c src/platform/mmu.c src/platform/mmu_arm926.S)
target_compile_definitions(vflash_mmu_animation PRIVATE VFLASH_DOUBLE_BUFFER=1 VFLASH_ENABLE_MMU=1)
target_link_libraries(vflash_mmu_animation PRIVATE gcc)
vflash_add_disc(mmu_animation_disc_image vflash_mmu_animation mmu/animation)

set(gpu_sources src/gpu/gpu.c src/gpu/gpu_software.c src/gpu/gpu_zevio.c src/graphics/renderer.c src/graphics/sdl_render.c)
vflash_add_program(vflash_gpu_software firmware-disc.ld examples/gpu/software.c src/platform/clock.c ${gpu_sources})
vflash_add_disc(gpu_software_disc_image vflash_gpu_software gpu/software)
vflash_add_program(vflash_gpu_mmu firmware-disc.ld examples/gpu/software.c src/platform/clock.c ${gpu_sources}
    src/platform/mmu.c src/platform/mmu_arm926.S)
target_compile_definitions(vflash_gpu_mmu PRIVATE VFLASH_GPU_MMU=1)
vflash_add_disc(gpu_mmu_disc_image vflash_gpu_mmu gpu/mmu)
vflash_add_program(vflash_gpu_native firmware-disc.ld examples/gpu/native.c src/platform/clock.c)
vflash_add_disc(gpu_native_disc_image vflash_gpu_native gpu/native)

foreach(mmu IN ITEMS off on)
    set(tiled_final 0)
    foreach(view IN ITEMS a b linear)
        set(target vflash_gpu_tiled_${mmu}_${view})
        vflash_add_program(${target} firmware-disc.ld examples/gpu/tiled.c src/platform/clock.c)
        target_compile_definitions(${target} PRIVATE GPU_TILED_FINAL=${tiled_final})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/integrated.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_FRAME_FINAL=${final_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/sprite.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_SPRITE_FINAL=${sprite_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/transparent.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_TRANSPARENT_FINAL=${transparent_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/alpha.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_ALPHA_FINAL=${alpha_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/modes.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_MODES_FINAL=${modes_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/renderer_demo.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_RENDERER_FINAL=${renderer_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/lifecycle.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_RENDERER_FINAL=${lifecycle_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/sdl_demo.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_RENDERER_FINAL=${sdl_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
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
        vflash_add_program(${target} firmware-disc.ld examples/gpu/triangle.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target} PRIVATE GPU_TRIANGLE_FINAL=${triangle_view})
        if(mmu STREQUAL "on")
            target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
            target_compile_definitions(${target} PRIVATE GPU_TRIANGLE_MMU=1)
        endif()
        vflash_add_disc(gpu_triangle_${mmu}_${view}_disc_image ${target} gpu/triangle/${mmu}/${view})

        set(target_depth vflash_gpu_triangle_depth_${mmu}_${view})
        vflash_add_program(${target_depth} firmware-disc.ld examples/gpu/triangle_depth.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target_depth} PRIVATE GPU_TRIANGLE_FINAL=${triangle_view})
        if(mmu STREQUAL "on")
            target_sources(${target_depth} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
            target_compile_definitions(${target_depth} PRIVATE GPU_TRIANGLE_MMU=1)
        endif()
        vflash_add_disc(gpu_triangle_depth_${mmu}_${view}_disc_image ${target_depth} gpu/triangle_depth/${mmu}/${view})

        set(target_tex vflash_gpu_triangle_tex_${mmu}_${view})
        vflash_add_program(${target_tex} firmware-disc.ld examples/gpu/triangle_tex.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target_tex} PRIVATE GPU_TRIANGLE_FINAL=${triangle_view})
        if(mmu STREQUAL "on")
            target_sources(${target_tex} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
            target_compile_definitions(${target_tex} PRIVATE GPU_TRIANGLE_MMU=1)
        endif()
        vflash_add_disc(gpu_triangle_tex_${mmu}_${view}_disc_image ${target_tex} gpu/triangle_tex/${mmu}/${view})

        set(target_cube vflash_gpu_cube_${mmu}_${view})
        vflash_add_program(${target_cube} firmware-disc.ld examples/gpu/cube.c src/platform/clock.c ${gpu_sources})
        target_compile_definitions(${target_cube} PRIVATE GPU_TRIANGLE_FINAL=${triangle_view})
        if(mmu STREQUAL "on")
            target_sources(${target_cube} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
            target_compile_definitions(${target_cube} PRIVATE GPU_TRIANGLE_MMU=1)
        endif()
        vflash_add_disc(gpu_cube_${mmu}_${view}_disc_image ${target_cube} gpu/cube/${mmu}/${view})

        math(EXPR triangle_view "${triangle_view}+1")
    endforeach()
endforeach()

foreach(mmu IN ITEMS off on)
    set(input_target vflash_input_${mmu})
    vflash_add_program(${input_target} firmware-disc.ld examples/input/controller_demo.c src/platform/clock.c)
    target_compile_definitions(${input_target} PRIVATE VFLASH_DOUBLE_BUFFER=1)
    if(mmu STREQUAL "on")
        target_sources(${input_target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
        target_compile_definitions(${input_target} PRIVATE VFLASH_ENABLE_MMU=1)
    endif()
    vflash_add_disc(input_${mmu}_disc_image ${input_target} input/${mmu})

    set(audio_target vflash_audio_${mmu})
    vflash_add_program(${audio_target} firmware-disc.ld examples/audio/audio_demo.c src/platform/clock.c)
    target_compile_definitions(${audio_target} PRIVATE VFLASH_DOUBLE_BUFFER=1)
    if(mmu STREQUAL "on")
        target_sources(${audio_target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
        target_compile_definitions(${audio_target} PRIVATE VFLASH_ENABLE_MMU=1)
    endif()
    vflash_add_disc(audio_${mmu}_disc_image ${audio_target} audio/${mmu})
endforeach()

option(VFLASH_ENABLE_SDL2 "Build real upstream SDL2 library and tests" ON)

if(VFLASH_ENABLE_SDL2 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/include/SDL.h")
    set(sdl2_sources
        external/SDL2/src/SDL.c
        external/SDL2/src/SDL_assert.c
        external/SDL2/src/SDL_dataqueue.c
        external/SDL2/src/SDL_error.c
        external/SDL2/src/SDL_hints.c
        external/SDL2/src/SDL_guid.c
        external/SDL2/src/SDL_list.c
        external/SDL2/src/SDL_log.c
        external/SDL2/src/SDL_utils.c
        external/SDL2/src/atomic/SDL_atomic.c
        external/SDL2/src/atomic/SDL_spinlock.c
        external/SDL2/src/cpuinfo/SDL_cpuinfo.c
        external/SDL2/src/events/SDL_clipboardevents.c
        external/SDL2/src/events/SDL_displayevents.c
        external/SDL2/src/events/SDL_dropevents.c
        external/SDL2/src/events/SDL_events.c
        external/SDL2/src/events/SDL_gesture.c
        external/SDL2/src/events/SDL_keyboard.c
        external/SDL2/src/events/SDL_mouse.c
        external/SDL2/src/events/SDL_quit.c
        external/SDL2/src/events/SDL_touch.c
        external/SDL2/src/events/SDL_windowevents.c
        external/SDL2/src/file/SDL_rwops.c
        external/SDL2/src/filesystem/dummy/SDL_sysfilesystem.c
        external/SDL2/src/stdlib/SDL_getenv.c
        external/SDL2/src/stdlib/SDL_iconv.c
        external/SDL2/src/stdlib/SDL_malloc.c
        external/SDL2/src/stdlib/SDL_qsort.c
        external/SDL2/src/stdlib/SDL_stdlib.c
        external/SDL2/src/stdlib/SDL_string.c
        external/SDL2/src/stdlib/SDL_strtokr.c
        external/SDL2/src/stdlib/SDL_crc16.c
        external/SDL2/src/stdlib/SDL_crc32.c
        external/SDL2/src/thread/SDL_thread.c
        external/SDL2/src/thread/generic/SDL_syscond.c
        external/SDL2/src/thread/generic/SDL_sysmutex.c
        external/SDL2/src/thread/generic/SDL_syssem.c
        external/SDL2/src/thread/generic/SDL_systhread.c
        external/SDL2/src/thread/generic/SDL_systls.c
        external/SDL2/src/video/SDL_video.c
        external/SDL2/src/video/SDL_surface.c
        external/SDL2/src/video/SDL_pixels.c
        external/SDL2/src/video/SDL_rect.c
        external/SDL2/src/video/SDL_blit.c
        external/SDL2/src/video/SDL_blit_0.c
        external/SDL2/src/video/SDL_blit_1.c
        external/SDL2/src/video/SDL_blit_A.c
        external/SDL2/src/video/SDL_blit_N.c
        external/SDL2/src/video/SDL_blit_auto.c
        external/SDL2/src/video/SDL_blit_copy.c
        external/SDL2/src/video/SDL_blit_slow.c
        external/SDL2/src/video/SDL_bmp.c
        external/SDL2/src/video/SDL_fillrect.c
        external/SDL2/src/video/SDL_RLEaccel.c
        external/SDL2/src/video/SDL_stretch.c
        external/SDL2/src/video/SDL_yuv.c
        external/SDL2/src/video/yuv2rgb/yuv_rgb_std.c
        external/SDL2/src/render/SDL_render.c
        external/SDL2/src/render/SDL_yuv_sw.c
        external/SDL2/src/render/SDL_d3dmath.c
        external/SDL2/src/render/software/SDL_blendfillrect.c
        external/SDL2/src/render/software/SDL_blendline.c
        external/SDL2/src/render/software/SDL_blendpoint.c
        external/SDL2/src/render/software/SDL_drawline.c
        external/SDL2/src/render/software/SDL_drawpoint.c
        external/SDL2/src/render/software/SDL_render_sw.c
        external/SDL2/src/render/software/SDL_rotate.c
        external/SDL2/src/render/software/SDL_triangle.c
        external/SDL2/src/timer/SDL_timer.c
        external/SDL2/src/libm/e_atan2.c
        external/SDL2/src/libm/e_exp.c
        external/SDL2/src/libm/e_fmod.c
        external/SDL2/src/libm/e_log10.c
        external/SDL2/src/libm/e_log.c
        external/SDL2/src/libm/e_pow.c
        external/SDL2/src/libm/e_rem_pio2.c
        external/SDL2/src/libm/e_sqrt.c
        external/SDL2/src/libm/k_cos.c
        external/SDL2/src/libm/k_rem_pio2.c
        external/SDL2/src/libm/k_sin.c
        external/SDL2/src/libm/k_tan.c
        external/SDL2/src/libm/s_atan.c
        external/SDL2/src/libm/s_copysign.c
        external/SDL2/src/libm/s_cos.c
        external/SDL2/src/libm/s_fabs.c
        external/SDL2/src/libm/s_floor.c
        external/SDL2/src/libm/s_scalbn.c
        external/SDL2/src/libm/s_sin.c
        external/SDL2/src/libm/s_tan.c
        src/graphics/sdl2_vflash/SDL_vflashvideo.c
        src/graphics/sdl2_vflash/SDL_vflashrender.c
        src/graphics/sdl2_vflash/SDL_vflashruntime.c
        external/SDL2/src/joystick/SDL_joystick.c
        external/SDL2/src/joystick/SDL_gamecontroller.c
        external/SDL2/src/joystick/controller_type.c
        src/graphics/sdl2_vflash/SDL_vflashjoystick.c
        external/SDL2/src/audio/SDL_audio.c
        external/SDL2/src/audio/SDL_audiocvt.c
        external/SDL2/src/audio/SDL_audiodev.c
        external/SDL2/src/audio/SDL_audiotypecvt.c
        external/SDL2/src/audio/SDL_mixer.c
        external/SDL2/src/audio/SDL_wave.c
        src/graphics/sdl2_vflash/SDL_vflashaudio.c
        src/platform/cd.c
        src/platform/fs.c
    )

    add_library(SDL2 STATIC ${sdl2_sources})
    target_include_directories(SDL2 PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include/sdl2_vflash"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/include"
    )
    target_include_directories(SDL2 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/src"
    )
    target_compile_definitions(SDL2 PUBLIC
        DYNAPI_NEEDS_DLOPEN=1
    )
    set_source_files_properties(external/SDL2/src/file/SDL_rwops.c PROPERTIES
        COMPILE_DEFINITIONS SDL_BUILD_RWOPS=1
    )
    set(sdl2_arch_flags -mcpu=arm926ej-s -marm -mfloat-abi=soft -mlittle-endian -mabi=aapcs -fno-short-enums)
    target_compile_options(SDL2 PRIVATE
        ${sdl2_arch_flags} -Os -g3 -ffreestanding -fno-builtin
        -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
        -w
    )

    set(sdl2_gpu_sources src/gpu/gpu.c src/gpu/gpu_software.c src/gpu/gpu_zevio.c src/graphics/renderer.c)

    foreach(mmu IN ITEMS off on)
        set(sdl2_view 0)
        foreach(view IN ITEMS a b linear)
            set(target vflash_gpu_sdl2_${mmu}_${view})
            vflash_add_program(${target} firmware-disc.ld
                examples/gpu/sdl2_testsprite.c
                external/SDL2/test/testutils.c
                src/platform/clock.c
                ${sdl2_gpu_sources}
            )
            target_include_directories(${target} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/include/sdl2_vflash"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/test"
            )
            target_compile_definitions(${target} PRIVATE
                DYNAPI_NEEDS_DLOPEN=1
                GPU_RENDERER_FINAL=${sdl2_view}
            )
            if(mmu STREQUAL "on")
                target_sources(${target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
                target_compile_definitions(${target} PRIVATE GPU_SDL2_MMU=1)
            endif()
            target_link_libraries(${target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
            vflash_add_disc(gpu_sdl2_${mmu}_${view}_disc_image ${target} gpu/sdl2/${mmu}/${view})

            set(draw_target vflash_gpu_sdl2_draw_${mmu}_${view})
            vflash_add_program(${draw_target} firmware-disc.ld
                examples/gpu/sdl2_testdraw.c
                src/platform/clock.c
                ${sdl2_gpu_sources}
            )
            target_include_directories(${draw_target} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/include/sdl2_vflash"
                "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/include"
            )
            target_compile_definitions(${draw_target} PRIVATE
                DYNAPI_NEEDS_DLOPEN=1
                GPU_RENDERER_FINAL=${sdl2_view}
            )
            if(mmu STREQUAL "on")
                target_sources(${draw_target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
                target_compile_definitions(${draw_target} PRIVATE GPU_SDL2_MMU=1)
            endif()
            target_link_libraries(${draw_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
            vflash_add_disc(gpu_sdl2_draw_${mmu}_${view}_disc_image ${draw_target} gpu/sdl2_draw/${mmu}/${view})

            math(EXPR sdl2_view "${sdl2_view}+1")
        endforeach()
    endforeach()

    foreach(mmu IN ITEMS off on)
        set(sdl2_input_target vflash_sdl2_input_${mmu})
        vflash_add_program(${sdl2_input_target} firmware-disc.ld
            examples/input/sdl2_controller_demo.c
            src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${sdl2_input_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${sdl2_input_target} PRIVATE
            DYNAPI_NEEDS_DLOPEN=1
        )
        if(mmu STREQUAL "on")
            target_sources(${sdl2_input_target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
            target_compile_definitions(${sdl2_input_target} PRIVATE VFLASH_ENABLE_MMU=1)
        endif()
        target_link_libraries(${sdl2_input_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(sdl2_input_${mmu}_disc_image ${sdl2_input_target} input/sdl2/${mmu})

        set(sdl2_audio_target vflash_sdl2_audio_${mmu})
        vflash_add_program(${sdl2_audio_target} firmware-disc.ld
            examples/audio/sdl2_audio_demo.c
            src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${sdl2_audio_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${sdl2_audio_target} PRIVATE
            DYNAPI_NEEDS_DLOPEN=1
        )
        if(mmu STREQUAL "on")
            target_sources(${sdl2_audio_target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
            target_compile_definitions(${sdl2_audio_target} PRIVATE VFLASH_ENABLE_MMU=1)
        endif()
        target_link_libraries(${sdl2_audio_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc(sdl2_audio_${mmu}_disc_image ${sdl2_audio_target} audio/sdl2/${mmu})

        set(sdl2_asset_target vflash_sdl2_asset_${mmu})
        vflash_add_program(${sdl2_asset_target} firmware-disc.ld
            examples/fs/asset_demo.c
            src/platform/clock.c
            ${sdl2_gpu_sources}
        )
        target_include_directories(${sdl2_asset_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include/sdl2_vflash"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL2/include"
        )
        target_compile_definitions(${sdl2_asset_target} PRIVATE
            DYNAPI_NEEDS_DLOPEN=1
        )
        if(mmu STREQUAL "on")
            target_sources(${sdl2_asset_target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
            target_compile_definitions(${sdl2_asset_target} PRIVATE VFLASH_ENABLE_MMU=1)
        endif()
        target_link_libraries(${sdl2_asset_target} PRIVATE -Wl,--start-group SDL2 gcc -Wl,--end-group)
        vflash_add_disc_with_assets(sdl2_asset_${mmu}_disc_image ${sdl2_asset_target} fs/sdl2/${mmu} "${CMAKE_CURRENT_SOURCE_DIR}/examples/fs/assets")
    endforeach()
endif()

foreach(mmu IN ITEMS off on)
    set(fs_native_target vflash_fs_${mmu})
    vflash_add_program(${fs_native_target} firmware-disc.ld
        examples/fs/fs_demo.c
        src/platform/cd.c
        src/platform/fs.c
        src/platform/clock.c
    )
    if(mmu STREQUAL "on")
        target_sources(${fs_native_target} PRIVATE src/platform/mmu.c src/platform/mmu_arm926.S)
        target_compile_definitions(${fs_native_target} PRIVATE VFLASH_ENABLE_MMU=1)
    endif()
    vflash_add_disc_with_assets(fs_${mmu}_disc_image ${fs_native_target} fs/native/${mmu} "${CMAKE_CURRENT_SOURCE_DIR}/examples/fs/assets")
endforeach()


