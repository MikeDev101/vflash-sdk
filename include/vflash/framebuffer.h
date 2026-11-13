#ifndef VFLASH_FRAMEBUFFER_H
#define VFLASH_FRAMEBUFFER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Native linear framebuffer constants: BGR555 pixel format at 320x240.
//
enum
{
    VFLASH_BGR555 = 1,
    VFLASH_WIDTH  = 320,
    VFLASH_HEIGHT = 240
};

//
// Descriptor for a linear pixel buffer in memory.
//
typedef struct
{
    uint16_t *pixels;         // Pointer to pixel data
    uint32_t width;           // Active width in pixels
    uint32_t height;          // Active height in pixels
    uint32_t stride;          // Row pitch in pixels, not bytes
    uint32_t capacity_pixels; // Total allocated pixel capacity
    uint32_t pixel_format;    // Pixel format constant (VFLASH_BGR555)
} vflash_framebuffer_t;

#ifdef __cplusplus
}
#endif

#endif // VFLASH_FRAMEBUFFER_H
