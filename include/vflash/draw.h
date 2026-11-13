#ifndef VFLASH_DRAW_H
#define VFLASH_DRAW_H

#include "vflash/framebuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// Validate that a framebuffer descriptor has valid dimensions, stride,
// capacity, pixel pointer, and VFLASH_BGR555 format.
//
// fb: Framebuffer descriptor to validate.
//
// Returns 1 if valid, or 0 otherwise.
//
int draw_valid(const vflash_framebuffer_t *fb);

//
// Convert a 24-bit 0xRRGGBB color into a 16-bit V.Flash BGR555 pixel.
//
// rgb: 24-bit RGB color (0x00RRGGBB).
//
// Returns the packed 16-bit BGR555 color value.
//
uint16_t draw_color(uint32_t rgb);

//
// Fill the entire framebuffer with a solid 24-bit RGB color.
//
// fb:  Target framebuffer descriptor.
// rgb: 24-bit fill color (0x00RRGGBB).
//
void draw_clear(vflash_framebuffer_t *fb, uint32_t rgb);

//
// Plot a single clipped pixel at (x, y) on the framebuffer.
//
// fb:  Target framebuffer descriptor.
// x:   Horizontal pixel coordinate.
// y:   Vertical pixel coordinate.
// rgb: 24-bit pixel color (0x00RRGGBB).
//
void draw_pixel(vflash_framebuffer_t *fb, int32_t x, int32_t y, uint32_t rgb);

//
// Draw a clipped solid rectangle on the framebuffer.
//
// fb:     Target framebuffer descriptor.
// x:      Left edge X coordinate.
// y:      Top edge Y coordinate.
// width:  Rectangle width in pixels.
// height: Rectangle height in pixels.
// rgb:    24-bit fill color (0x00RRGGBB).
//
void draw_rect(vflash_framebuffer_t *fb, int32_t x, int32_t y,
               int32_t width, int32_t height, uint32_t rgb);

//
// Render a null-terminated ASCII string using the 5x7 bitmap font.
//
// fb:    Target framebuffer descriptor.
// x:     Starting X coordinate.
// y:     Starting Y coordinate.
// text:  Null-terminated ASCII string.
// rgb:   24-bit foreground color (0x00RRGGBB).
// scale: Integer pixel scale factor (>= 1).
//
void draw_text(vflash_framebuffer_t *fb, int32_t x, int32_t y,
               const char *text, uint32_t rgb, uint32_t scale);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_DRAW_H
