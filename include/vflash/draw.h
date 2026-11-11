#ifndef VFLASH_DRAW_H
#define VFLASH_DRAW_H
#include "vflash/framebuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

int draw_valid(const vflash_framebuffer_t *fb);
uint16_t draw_color(uint32_t rgb);
void draw_clear(vflash_framebuffer_t *fb, uint32_t rgb);
void draw_pixel(vflash_framebuffer_t *fb, int32_t x, int32_t y, uint32_t rgb);
void draw_rect(vflash_framebuffer_t *fb, int32_t x, int32_t y,
               int32_t width, int32_t height, uint32_t rgb);
void draw_text(vflash_framebuffer_t *fb, int32_t x, int32_t y,
               const char *text, uint32_t rgb, uint32_t scale);

#ifdef __cplusplus
}
#endif

#endif
