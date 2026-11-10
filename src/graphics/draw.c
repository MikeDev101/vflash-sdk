#include "vflash/draw.h"
#include "vflash/font.h"

int draw_valid(const vflash_framebuffer_t *fb)
{
    return fb && fb->pixels && fb->pixel_format == VFLASH_BGR555 &&
        fb->width && fb->height && fb->width <= INT32_MAX && fb->height <= INT32_MAX &&
        fb->stride >= fb->width &&
        (uint64_t)fb->stride * fb->height <= fb->capacity_pixels;
}

uint16_t draw_color(uint32_t rgb)
{
    /* Opaque xBGR1555: bit 15 stays clear; red occupies the LOW five bits. */
    return (uint16_t)(((rgb >> 19) & 31u) | ((rgb >> 6) & 0x3E0u) |
                      ((rgb << 7) & 0x7C00u));
}

void draw_pixel(vflash_framebuffer_t *fb, int32_t x, int32_t y, uint32_t rgb)
{
    if (draw_valid(fb) && x >= 0 && y >= 0 &&
        (uint32_t)x < fb->width && (uint32_t)y < fb->height) {
        fb->pixels[(uint32_t)y * fb->stride + (uint32_t)x] = draw_color(rgb);
    }
}

void draw_rect(vflash_framebuffer_t *fb, int32_t x, int32_t y,
               int32_t width, int32_t height, uint32_t rgb)
{
    if (!draw_valid(fb) || width <= 0 || height <= 0) return;
    /* Widen before adding: even INT32_MIN/MAX coordinates cannot overflow. */
    int64_t left = x, top = y, right = (int64_t)x + width, bottom = (int64_t)y + height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > fb->width) right = fb->width;
    if (bottom > fb->height) bottom = fb->height;
    uint16_t color = draw_color(rgb);
    for (int64_t row = top; row < bottom; ++row) {
        uint16_t *pixels = fb->pixels + (uint32_t)row * fb->stride;
        for (int64_t column = left; column < right; ++column) pixels[(uint32_t)column] = color;
    }
}

void draw_clear(vflash_framebuffer_t *fb, uint32_t rgb)
{
    if (draw_valid(fb)) draw_rect(fb, 0, 0, (int32_t)fb->width, (int32_t)fb->height, rgb);
}

void draw_text(vflash_framebuffer_t *fb, int32_t x, int32_t y,
               const char *text, uint32_t rgb, uint32_t scale)
{
    if (!draw_valid(fb) || !text || !scale || scale > 16) return;
    int64_t cursor = x;
    for (; *text && cursor < fb->width; ++text, cursor += 6 * scale) {
        const uint8_t *glyph = font_glyph((unsigned char)*text);
        for (uint32_t row = 0; row < 7; ++row) {
            int64_t py = (int64_t)y + row * scale;
            if (py > INT32_MAX) continue;
            for (uint32_t column = 0; column < 5; ++column) {
                int64_t px = cursor + column * scale;
                if ((glyph[row] & (16u >> column)) && px <= INT32_MAX) {
                    draw_rect(fb, (int32_t)px, (int32_t)py,
                              (int32_t)scale, (int32_t)scale, rgb);
                }
            }
        }
    }
}
