#include "demo.h"
#include "draw.h"

void demo_render(vflash_framebuffer_t *fb)
{
    if (!draw_valid(fb)) return;
    draw_clear(fb, 0x101820u);
    /* 13 glyph cells, final spacer excluded: 77 pixels at scale 3. */
    draw_text(fb, ((int32_t)fb->width - 231) / 2, 45,
              "Hello, World!", 0xFFFFFFu, 3);
    int32_t bar_width = (int32_t)(fb->width / 4u);
    int32_t left = ((int32_t)fb->width - 3 * bar_width) / 2;
    draw_rect(fb, left - 3, 107, 3 * bar_width + 6, 76, 0xFFFFFFu);
    draw_rect(fb, left, 110, bar_width, 70, 0xFF0000u);
    draw_rect(fb, left + bar_width, 110, bar_width, 70, 0x00FF00u);
    draw_rect(fb, left + 2 * bar_width, 110, bar_width, 70, 0x0000FFu);
}
