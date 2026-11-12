if(NOT DEFINED VFLASH_SDK_DIR)
    get_filename_component(VFLASH_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

option(VFLASH_ENABLE_SDL2 "Build real upstream SDL2 library and tests" ON)

if(VFLASH_ENABLE_SDL2 AND EXISTS "${VFLASH_SDK_DIR}/external/SDL2/include/SDL.h")
    set(sdl2_sources
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL_assert.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL_dataqueue.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL_error.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL_hints.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL_guid.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL_list.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL_log.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/SDL_utils.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/atomic/SDL_atomic.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/atomic/SDL_spinlock.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/cpuinfo/SDL_cpuinfo.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_clipboardevents.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_displayevents.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_dropevents.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_events.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_gesture.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_keyboard.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_mouse.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_quit.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_touch.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/events/SDL_windowevents.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/file/SDL_rwops.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/filesystem/dummy/SDL_sysfilesystem.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_getenv.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_iconv.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_malloc.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_qsort.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_stdlib.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_string.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_strtokr.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_crc16.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/stdlib/SDL_crc32.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/thread/SDL_thread.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/thread/generic/SDL_syscond.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/thread/generic/SDL_sysmutex.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/thread/generic/SDL_syssem.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/thread/generic/SDL_systhread.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/thread/generic/SDL_systls.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_video.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_surface.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_pixels.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_rect.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_blit.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_blit_0.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_blit_1.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_blit_A.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_blit_N.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_blit_auto.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_blit_copy.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_blit_slow.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_bmp.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_fillrect.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_RLEaccel.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_stretch.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/SDL_yuv.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/video/yuv2rgb/yuv_rgb_std.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/SDL_render.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/SDL_yuv_sw.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/SDL_d3dmath.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/software/SDL_blendfillrect.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/software/SDL_blendline.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/software/SDL_blendpoint.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/software/SDL_drawline.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/software/SDL_drawpoint.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/software/SDL_render_sw.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/software/SDL_rotate.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/render/software/SDL_triangle.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/timer/SDL_timer.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/e_atan2.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/e_exp.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/e_fmod.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/e_log10.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/e_log.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/e_pow.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/e_rem_pio2.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/e_sqrt.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/k_cos.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/k_rem_pio2.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/k_sin.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/k_tan.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/s_atan.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/s_copysign.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/s_cos.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/s_fabs.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/s_floor.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/s_scalbn.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/s_sin.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/libm/s_tan.c
        ${VFLASH_SDK_DIR}/src/graphics/sdl2_vflash/SDL_vflashvideo.c
        ${VFLASH_SDK_DIR}/src/graphics/sdl2_vflash/SDL_vflashrender.c
        ${VFLASH_SDK_DIR}/src/graphics/sdl2_vflash/SDL_vflashruntime.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/joystick/SDL_joystick.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/joystick/SDL_gamecontroller.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/joystick/controller_type.c
        ${VFLASH_SDK_DIR}/src/graphics/sdl2_vflash/SDL_vflashjoystick.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/audio/SDL_audio.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/audio/SDL_audiocvt.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/audio/SDL_audiodev.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/audio/SDL_audiotypecvt.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/audio/SDL_mixer.c
        ${VFLASH_SDK_DIR}/external/SDL2/src/audio/SDL_wave.c
        ${VFLASH_SDK_DIR}/src/graphics/sdl2_vflash/SDL_vflashaudio.c
        ${VFLASH_SDK_DIR}/src/platform/cd.c
        ${VFLASH_SDK_DIR}/src/platform/fs.c
        ${VFLASH_SDK_DIR}/src/gpu/gpu.c
        ${VFLASH_SDK_DIR}/src/gpu/gpu_software.c
        ${VFLASH_SDK_DIR}/src/gpu/gpu_zevio.c
        ${VFLASH_SDK_DIR}/src/graphics/renderer.c
    )

    add_library(SDL2 STATIC ${sdl2_sources})
    target_include_directories(SDL2 PUBLIC
        "${VFLASH_SDK_DIR}/include/sdl2_vflash"
        "${VFLASH_SDK_DIR}/external/SDL2/include"
    )
    target_include_directories(SDL2 PRIVATE
        "${VFLASH_SDK_DIR}/include"
        "${VFLASH_SDK_DIR}/external/SDL2/src"
    )
    target_compile_definitions(SDL2 PUBLIC
        DYNAPI_NEEDS_DLOPEN=1
    )
    set_source_files_properties(${VFLASH_SDK_DIR}/external/SDL2/src/file/SDL_rwops.c PROPERTIES
        COMPILE_DEFINITIONS SDL_BUILD_RWOPS=1
    )
    set(sdl2_arch_flags -mcpu=arm926ej-s -marm -mfloat-abi=soft -mlittle-endian -mabi=aapcs -fno-short-enums)
    if(NOT DEFINED VFLASH_OPT_LEVEL)
        set(VFLASH_OPT_LEVEL "-O3" CACHE STRING "Compiler optimization level (-O1, -O2, -O3, -Os)")
    endif()
    target_compile_options(SDL2 PRIVATE
        ${sdl2_arch_flags} ${VFLASH_OPT_LEVEL} -g3 -ffreestanding -fno-builtin
        -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
        -w
    )
endif()
