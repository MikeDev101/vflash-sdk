#ifndef VFLASH_ANIMATION_H
#define VFLASH_ANIMATION_H
#include "framebuffer.h"
typedef struct { int32_t x, y, dx, dy; } animation_state_t;
void animation_init(animation_state_t *state);
void animation_step(animation_state_t *state);
void animation_render(vflash_framebuffer_t *fb, const animation_state_t *state,
                      uint32_t fps, uint32_t render_us);
#endif
