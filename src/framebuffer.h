#ifndef VFLASH_FRAMEBUFFER_H
#define VFLASH_FRAMEBUFFER_H
#include <stdint.h>

enum { VFLASH_BGR555 = 1, VFLASH_WIDTH = 320, VFLASH_HEIGHT = 240 };
typedef struct {
    uint16_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride; /* pixels, not bytes */
    uint32_t capacity_pixels;
    uint32_t pixel_format;
} vflash_framebuffer_t;
#endif
