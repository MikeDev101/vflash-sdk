#ifndef SDL_config_h_
#define SDL_config_h_

#define SDL_config_minimal_h_

#include "SDL_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* Standard types and varargs available in freestanding toolchain */
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1

#ifdef __GNUC__
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1
#endif

/* Freestanding: no hosted C library */
#undef HAVE_STDLIB_H
#undef HAVE_STDIO_H
#undef HAVE_STRING_H
#undef HAVE_STRINGS_H
#undef HAVE_CTYPE_H
#undef HAVE_MATH_H

/* Memory management: freestanding bounded allocator provided by V.Flash runtime */
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1

/* Freestanding declarations for allocator hooks */
extern void *malloc(size_t size);
extern void *calloc(size_t nmemb, size_t size);
extern void *realloc(void *ptr, size_t size);
extern void free(void *ptr);

/* Disabled subsystems */
#define SDL_AUDIO_DISABLED 1
#define SDL_JOYSTICK_DISABLED 1
#define SDL_HAPTIC_DISABLED 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_SENSOR_DISABLED 1
#define SDL_LOADSO_DISABLED 1
#define SDL_THREADS_DISABLED 1
#define SDL_FILESYSTEM_DISABLED 1
#define SDL_POWER_DISABLED 1

/* Timer support */
#define SDL_TIMERS_DISABLED 1

/* Disable dynapi for static linking without dlopen */
#define DYNAPI_NEEDS_DLOPEN 1
#undef HAVE_DLOPEN

/* Intercept SDL_RWFromFile for virtual embedded asset loader (only inside SDL_rwops.c) */
#if defined(SDL_BUILD_RWOPS)
#define SDL_RWFromFile SDL_RWFromFile_Upstream
#endif

/* Video driver: alias DUMMY to VFLASH */
#define SDL_VIDEO_DRIVER_DUMMY 1
#define DUMMY_bootstrap VFLASH_bootstrap

/* Render drivers: V.Flash hardware accelerated + Software fallback */
#define SDL_VIDEO_RENDER_PSP 1
#define PSP_RenderDriver VFLASH_RenderDriver
#define SDL_VIDEO_RENDER_SW 1

#endif /* SDL_config_h_ */
