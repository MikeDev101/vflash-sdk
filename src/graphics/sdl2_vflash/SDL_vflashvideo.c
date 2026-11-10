#include "SDL_internal.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "video/SDL_sysvideo.h"
#include "video/SDL_pixels_c.h"
#include "events/SDL_events_c.h"

#include "vflash/video.h"
#include "vflash/draw.h"

#define VFLASH_VIDEO_DRIVER_NAME "vflash"

static int VFLASH_VideoInit(_THIS);
static void VFLASH_VideoQuit(_THIS);
static void VFLASH_PumpEvents(_THIS);

/* Software fallback framebuffer support */
static uint16_t s_window_fb_pixels[320 * 240] __attribute__((aligned(64)));
static vflash_framebuffer_t s_linear_fb;
static int s_linear_inited = 0;

static int VFLASH_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    (void)_this;
    (void)window;
    if (!s_linear_inited) {
        if (!vflash_video_init(&s_linear_fb)) return -1;
        s_linear_inited = 1;
    }
    *format = SDL_PIXELFORMAT_RGB555;
    *pitch = 320 * (int)sizeof(uint16_t);
    *pixels = s_window_fb_pixels;
    return 0;
}

static int VFLASH_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    (void)_this;
    (void)window;
    (void)rects;
    (void)numrects;
    if (!s_linear_inited) return -1;

    /* Copy software window pixels to linear scanout framebuffer */
    uint16_t *dst = (uint16_t *)s_linear_fb.pixels;
    const uint16_t *src = s_window_fb_pixels;
    for (uint32_t i = 0; i < 320 * 240; ++i) {
        dst[i] = src[i];
    }
    return vflash_video_present(&s_linear_fb) ? 0 : -1;
}

static void VFLASH_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    (void)_this;
    (void)window;
}

static void VFLASH_PumpEvents(_THIS)
{
    (void)_this;
    /* Event polling hook (input devices not yet present on bare metal) */
}

static void VFLASH_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *VFLASH_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }

    device->VideoInit = VFLASH_VideoInit;
    device->VideoQuit = VFLASH_VideoQuit;
    device->PumpEvents = VFLASH_PumpEvents;
    device->CreateWindowFramebuffer = VFLASH_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = VFLASH_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = VFLASH_DestroyWindowFramebuffer;
    device->free = VFLASH_DeleteDevice;

    return device;
}

VideoBootStrap VFLASH_bootstrap = {
    VFLASH_VIDEO_DRIVER_NAME,
    "VTech V.Flash Video Driver",
    VFLASH_CreateDevice,
    NULL
};

static int VFLASH_VideoInit(_THIS)
{
    SDL_DisplayMode mode;

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB555;
    mode.w = 320;
    mode.h = 240;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;

    if (SDL_AddBasicVideoDisplay(&mode) < 0) {
        return -1;
    }
    SDL_AddDisplayMode(&_this->displays[0], &mode);

    return 0;
}

static void VFLASH_VideoQuit(_THIS)
{
    (void)_this;
    if (s_linear_inited) {
        vflash_video_restore_linear();
    }
}
