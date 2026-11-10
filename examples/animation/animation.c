#include "animation.h"
#include "vflash/draw.h"

void animation_init(animation_state_t *state)
{
    *state = (animation_state_t){40, 100, 2, 3};
}

void animation_step(animation_state_t *state)
{
    state->x += state->dx;
    state->y += state->dy;
    if (state->x < 16) { state->x = 16; state->dx = -state->dx; }
    if (state->x > 280) { state->x = 280; state->dx = -state->dx; }
    if (state->y < 84) { state->y = 84; state->dy = -state->dy; }
    if (state->y > 180) { state->y = 180; state->dy = -state->dy; }
}

static void decimal(uint32_t value, char *text)
{
    char reverse[10];
    uint32_t count = 0;
    do { reverse[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    for (uint32_t i = 0; i < count; ++i) text[i] = reverse[count - i - 1];
    text[count] = 0;
}

void animation_render(vflash_framebuffer_t *fb, const animation_state_t *state,
                      uint32_t fps, uint32_t render_us)
{
    char number[11];
    draw_clear(fb, 0x101820);
    draw_text(fb, 20, 12, "FPS:", 0xFFFFFF, 2);
    decimal(fps, number);
    draw_text(fb, 80, 12, number, 0x60E0B0, 2);
    draw_text(fb, 170, 12, "CPU:", 0xFFFFFF, 2);
    /* Milliseconds to one decimal, from the preceding frame's measured work. */
    decimal(render_us / 1000, number);
    draw_text(fb, 230, 12, number, 0x60E0B0, 2);
    char fraction[5] = {'.', (char)('0' + render_us / 100 % 10), 'm', 's', 0};
    uint32_t digits = 1;
    for (uint32_t n = render_us / 1000; n >= 10; n /= 10) ++digits;
    draw_text(fb, 230 + (int32_t)digits * 12, 12, fraction, 0x60E0B0, 2);
    draw_text(fb, 44, 45, "Hello, World!", 0xFFFFFF, 3);
    draw_rect(fb, 12, 80, 296, 128, 0x52606D);
    draw_rect(fb, 14, 82, 292, 124, 0x101820);
    draw_rect(fb, state->x, state->y, 24, 24, 0xFFFFFF);
    draw_rect(fb, state->x + 2, state->y + 2, 20, 20, 0xFFCC40);
    draw_rect(fb, 40, 220, 80, 8, 0xFF0000);
    draw_rect(fb, 120, 220, 80, 8, 0x00FF00);
    draw_rect(fb, 200, 220, 80, 8, 0x0000FF);
}
