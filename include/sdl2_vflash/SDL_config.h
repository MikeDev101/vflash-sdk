#ifndef SDL_config_h_
#define SDL_config_h_

#define SDL_config_minimal_h_

#include "SDL_platform.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

//
// Standard types and varargs available in freestanding toolchain.
//
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1

#ifdef __GNUC__
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1
#endif

//
// Freestanding environment: no hosted C standard library headers.
//
#undef HAVE_STDLIB_H
#undef HAVE_STDIO_H
#undef HAVE_STRING_H
#undef HAVE_STRINGS_H
#undef HAVE_CTYPE_H
#undef HAVE_MATH_H

//
// Memory management: freestanding bounded allocator from V.Flash runtime.
//
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1

//
// Allocate size bytes from the bounded runtime heap.
//
// size: Number of bytes to allocate.
//
// Returns pointer to allocated block, or NULL on failure.
//
extern void *malloc(size_t size);

//
// Allocate zero-initialized storage for nmemb elements of size bytes.
//
// nmemb: Element count.
// size:  Byte size of each element.
//
// Returns pointer to zeroed block, or NULL on failure.
//
extern void *calloc(size_t nmemb, size_t size);

//
// Resize a previously allocated block to size bytes.
//
// ptr:  Existing allocation, or NULL to behave like malloc().
// size: New byte size.
//
// Returns pointer to resized block, or NULL on failure.
//
extern void *realloc(void *ptr, size_t size);

//
// Release a block allocated by malloc(), calloc(), or realloc().
//
// ptr: Pointer to block to free, or NULL for no-op.
//
extern void free(void *ptr);

//
// Audio subsystem: V.Flash native audio mapped to PSP bootstrap slot.
//
#define SDL_AUDIO_DRIVER_PSP 1
#define PSPAUDIO_bootstrap VFLASH_AudioBootStrap

//
// Joystick driver: V.Flash controller mapped to PSP driver slot.
//
#define SDL_JOYSTICK_PSP 1
#define SDL_PSP_JoystickDriver SDL_VFLASH_JoystickDriver
#define SDL_HAPTIC_DISABLED 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_SENSOR_DISABLED 1
#define SDL_LOADSO_DISABLED 1
#define SDL_THREADS_DISABLED 1
#define SDL_FILESYSTEM_DISABLED 1
#define SDL_POWER_DISABLED 1

//
// Timer support disabled in freestanding target.
//
#define SDL_TIMERS_DISABLED 1

//
// Disable dynamic API loading without dlopen.
//
#define DYNAPI_NEEDS_DLOPEN 1
#undef HAVE_DLOPEN

//
// Intercept SDL_RWFromFile for ISO 9660 disc loader inside SDL_rwops.c.
//
#if defined(SDL_BUILD_RWOPS)
#define SDL_RWFromFile SDL_RWFromFile_Upstream
#endif

//
// Video driver: alias DUMMY bootstrap to VFLASH.
//
#define SDL_VIDEO_DRIVER_DUMMY 1
#define DUMMY_bootstrap VFLASH_bootstrap

//
// Render drivers: V.Flash hardware accelerated and software fallback.
//
#define SDL_VIDEO_RENDER_PSP 1
#define PSP_RenderDriver VFLASH_RenderDriver
#define SDL_VIDEO_RENDER_SW 1

#endif // SDL_config_h_
