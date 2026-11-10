#ifndef SDL_VFLASH_H_
#define SDL_VFLASH_H_

#include "SDL.h"
#include "vflash/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Retrieve the underlying native Zevio 2D renderer from an SDL_Renderer */
vflash_renderer_t *vflash_sdl2_get_native_renderer(SDL_Renderer *renderer);

/* Direct pointers to the double-buffered 320x240 surfaces */
void *vflash_sdl2_get_surface_a(void);
void *vflash_sdl2_get_surface_b(void);

/* Retrieve hardware GPU statistics (lists, submission ticks, completion ticks) */
int vflash_sdl2_get_stats(SDL_Renderer *renderer, uint32_t *lists, uint32_t *sub_ticks, uint32_t *comp_ticks);

/* Memory heap statistics from the bounded allocator */
size_t vflash_sdl2_get_heap_used(void);
size_t vflash_sdl2_get_alloc_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_VFLASH_H_ */
