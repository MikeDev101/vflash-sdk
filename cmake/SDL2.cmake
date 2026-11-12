option(VFLASH_ENABLE_SDL2 "Build real upstream SDL2 library and tests" ON)

if(VFLASH_ENABLE_SDL2 AND EXISTS "${CMAKE_SOURCE_DIR}/external/SDL2/include/SDL.h")
    set(sdl2_sources
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL_assert.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL_dataqueue.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL_error.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL_hints.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL_guid.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL_list.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL_log.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/SDL_utils.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/atomic/SDL_atomic.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/atomic/SDL_spinlock.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/cpuinfo/SDL_cpuinfo.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_clipboardevents.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_displayevents.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_dropevents.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_events.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_gesture.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_keyboard.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_mouse.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_quit.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_touch.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/events/SDL_windowevents.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/file/SDL_rwops.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/filesystem/dummy/SDL_sysfilesystem.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_getenv.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_iconv.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_malloc.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_qsort.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_stdlib.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_string.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_strtokr.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_crc16.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/stdlib/SDL_crc32.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/thread/SDL_thread.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/thread/generic/SDL_syscond.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/thread/generic/SDL_sysmutex.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/thread/generic/SDL_syssem.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/thread/generic/SDL_systhread.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/thread/generic/SDL_systls.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_video.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_surface.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_pixels.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_rect.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_blit.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_blit_0.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_blit_1.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_blit_A.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_blit_N.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_blit_auto.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_blit_copy.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_blit_slow.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_bmp.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_fillrect.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_RLEaccel.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_stretch.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/SDL_yuv.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/video/yuv2rgb/yuv_rgb_std.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/SDL_render.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/SDL_yuv_sw.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/SDL_d3dmath.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/software/SDL_blendfillrect.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/software/SDL_blendline.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/software/SDL_blendpoint.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/software/SDL_drawline.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/software/SDL_drawpoint.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/software/SDL_render_sw.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/software/SDL_rotate.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/render/software/SDL_triangle.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/timer/SDL_timer.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/e_atan2.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/e_exp.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/e_fmod.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/e_log10.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/e_log.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/e_pow.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/e_rem_pio2.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/e_sqrt.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/k_cos.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/k_rem_pio2.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/k_sin.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/k_tan.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/s_atan.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/s_copysign.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/s_cos.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/s_fabs.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/s_floor.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/s_scalbn.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/s_sin.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/libm/s_tan.c
        ${CMAKE_SOURCE_DIR}/src/graphics/sdl2_vflash/SDL_vflashvideo.c
        ${CMAKE_SOURCE_DIR}/src/graphics/sdl2_vflash/SDL_vflashrender.c
        ${CMAKE_SOURCE_DIR}/src/graphics/sdl2_vflash/SDL_vflashruntime.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/joystick/SDL_joystick.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/joystick/SDL_gamecontroller.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/joystick/controller_type.c
        ${CMAKE_SOURCE_DIR}/src/graphics/sdl2_vflash/SDL_vflashjoystick.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/audio/SDL_audio.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/audio/SDL_audiocvt.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/audio/SDL_audiodev.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/audio/SDL_audiotypecvt.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/audio/SDL_mixer.c
        ${CMAKE_SOURCE_DIR}/external/SDL2/src/audio/SDL_wave.c
        ${CMAKE_SOURCE_DIR}/src/graphics/sdl2_vflash/SDL_vflashaudio.c
        ${CMAKE_SOURCE_DIR}/src/platform/cd.c
        ${CMAKE_SOURCE_DIR}/src/platform/fs.c
    )

    add_library(SDL2 STATIC ${sdl2_sources})
    target_include_directories(SDL2 PUBLIC
        "${CMAKE_SOURCE_DIR}/include/sdl2_vflash"
        "${CMAKE_SOURCE_DIR}/external/SDL2/include"
    )
    target_include_directories(SDL2 PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/external/SDL2/src"
    )
    target_compile_definitions(SDL2 PUBLIC
        DYNAPI_NEEDS_DLOPEN=1
    )
    set_source_files_properties(${CMAKE_SOURCE_DIR}/external/SDL2/src/file/SDL_rwops.c PROPERTIES
        COMPILE_DEFINITIONS SDL_BUILD_RWOPS=1
    )
    set(sdl2_arch_flags -mcpu=arm926ej-s -marm -mfloat-abi=soft -mlittle-endian -mabi=aapcs -fno-short-enums)
    target_compile_options(SDL2 PRIVATE
        ${sdl2_arch_flags} -Os -g3 -ffreestanding -fno-builtin
        -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
        -w
    )
endif()
