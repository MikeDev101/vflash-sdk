#ifndef SDL_VFLASH_H_
#define SDL_VFLASH_H_

#include "SDL.h"
#include "vflash/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// Retrieve the underlying native Zevio 2D renderer from an SDL_Renderer.
//
// renderer: Active SDL_Renderer created by the V.Flash SDL2 video driver.
//
// Returns a pointer to vflash_renderer_t, or NULL if unavailable.
//
vflash_renderer_t *vflash_sdl2_get_native_renderer(SDL_Renderer *renderer);

//
// Retrieve the primary 320x240 Zevio tiled display surface pointer.
//
// Returns a pointer to surface A in aligned SDRAM.
//
void *vflash_sdl2_get_surface_a(void);

//
// Retrieve the secondary 320x240 Zevio tiled display surface pointer.
//
// Returns a pointer to surface B in aligned SDRAM.
//
void *vflash_sdl2_get_surface_b(void);

//
// Retrieve hardware GPU statistics from the SDL2 renderer backend.
//
// renderer:   Active SDL_Renderer instance.
// lists:      Optional output for submitted GPU command list count.
// sub_ticks:  Optional output for cumulative submission ticks.
// comp_ticks: Optional output for cumulative completion ticks.
//
// Returns 0 on success, or -1 on invalid renderer or stats query failure.
//
int vflash_sdl2_get_stats(SDL_Renderer *renderer, uint32_t *lists,
                          uint32_t *sub_ticks, uint32_t *comp_ticks);

//
// Query bytes currently allocated from the bounded SDL2 runtime heap.
//
// Returns used heap size in bytes.
//
size_t vflash_sdl2_get_heap_used(void);

//
// Query the cumulative number of allocations made by the SDL2 runtime heap.
//
// Returns total allocation count.
//
size_t vflash_sdl2_get_alloc_count(void);

#ifdef __cplusplus
}
#endif

#endif // SDL_VFLASH_H_
