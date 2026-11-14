#ifndef VFLASH_GPU_INTERNAL_H
#define VFLASH_GPU_INTERNAL_H

#include "vflash/gpu.h"

//
// Validate a linear or tiled GPU surface descriptor.
//
// s: Surface descriptor to check.
//
// Returns 1 if valid, or 0 otherwise.
//
int gpu_surface_valid(const vflash_gpu_surface_t *s);

//
// Software fallback rectangle fill on a linear GPU surface.
//
// s:   Target GPU surface.
// x:   Rectangle left X coordinate.
// y:   Rectangle top Y coordinate.
// w:   Rectangle width in pixels.
// h:   Rectangle height in pixels.
// rgb: 24-bit fill color (0x00RRGGBB).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_software_fill(vflash_gpu_surface_t *s,
                                      int32_t x, int32_t y,
                                      int32_t w, int32_t h, uint32_t rgb);

//
// Software fallback full-surface blit between linear GPU surfaces.
//
// d: Destination GPU surface.
// s: Source GPU surface.
// x: Destination X coordinate.
// y: Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_software_blit(vflash_gpu_surface_t *d,
                                      const vflash_gpu_surface_t *s,
                                      int32_t x, int32_t y);

//
// Validate a Zevio tiled surface descriptor and its alignment/size rules.
//
// s: Surface descriptor to check.
//
// Returns 1 if valid for Zevio hardware, or 0 otherwise.
//
int gpu_tiled_valid(const vflash_gpu_surface_t *s);

//
// Wait until the Zevio hardware GPU finishes all queued command lists.
//
// g: Initialized Zevio GPU instance.
//
// Returns VFLASH_GPU_OK on success, or VFLASH_GPU_TIMEOUT / error.
//
vflash_gpu_result_t gpu_zevio_wait(vflash_gpu_t *g);

//
// Retrieve cumulative Zevio GPU command list and timing counters.
//
// g:     Initialized Zevio GPU instance.
// stats: Output statistics structure.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_stats(vflash_gpu_t *g,
                                    vflash_gpu_stats_t *stats);

//
// Check whether the Zevio GPU backend is currently active/busy.
//
// g: GPU instance to query.
//
// Returns 1 if active, or 0 otherwise.
//
int gpu_zevio_in_use(vflash_gpu_t *g);

//
// Initialize the Zevio hardware GPU backend.
//
// g: GPU instance to initialize.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_init(vflash_gpu_t *g);

//
// Shut down the Zevio hardware GPU backend.
//
// g: GPU instance to shut down.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_shutdown(vflash_gpu_t *g);

//
// Present a sub-view of a Zevio tiled surface at (x, y).
//
// g: Initialized Zevio GPU instance.
// s: Tiled surface to display.
// w: Visible view width in pixels.
// h: Visible view height in pixels.
// x: Screen destination X coordinate.
// y: Screen destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_present_view(vflash_gpu_t *g,
                                           vflash_gpu_surface_t *s,
                                           uint32_t w, uint32_t h,
                                           int32_t x, int32_t y);

//
// Present a full Zevio tiled surface at (x, y).
//
// g: Initialized Zevio GPU instance.
// s: Tiled surface to display.
// x: Screen destination X coordinate.
// y: Screen destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_present(vflash_gpu_t *g,
                                      vflash_gpu_surface_t *s,
                                      int32_t x, int32_t y);

//
// Submit a Zevio hardware fill-rectangle command on a tiled surface.
//
// g:   Initialized Zevio GPU instance.
// s:   Target tiled surface.
// x:   Rectangle left X coordinate.
// y:   Rectangle top Y coordinate.
// w:   Rectangle width in pixels.
// h:   Rectangle height in pixels.
// rgb: 24-bit fill color (0x00RRGGBB).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_fill(vflash_gpu_t *g,
                                   vflash_gpu_surface_t *s,
                                   int32_t x, int32_t y,
                                   int32_t w, int32_t h, uint32_t rgb);

//
// Submit a Zevio D2 same-surface region copy command.
//
// g:  Initialized Zevio GPU instance.
// s:  Target tiled surface.
// sx: Source X coordinate.
// sy: Source Y coordinate.
// w:  Region width in pixels.
// h:  Region height in pixels.
// dx: Destination X coordinate.
// dy: Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_copy(vflash_gpu_t *g,
                                   vflash_gpu_surface_t *s,
                                   int32_t sx, int32_t sy,
                                   int32_t w, int32_t h,
                                   int32_t dx, int32_t dy);

//
// Upload a linear source region into a Zevio tiled destination surface.
//
// g:  Initialized Zevio GPU instance.
// d:  Destination tiled surface.
// s:  Source linear surface.
// sx: Source X coordinate.
// sy: Source Y coordinate.
// w:  Region width in pixels.
// h:  Region height in pixels.
// dx: Destination X coordinate.
// dy: Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_upload(vflash_gpu_t *g,
                                     vflash_gpu_surface_t *d,
                                     const vflash_gpu_surface_t *s,
                                     int32_t sx, int32_t sy,
                                     int32_t w, int32_t h,
                                     int32_t dx, int32_t dy);

//
// Submit a Zevio C8 sprite draw command with explicit texture format.
//
// g:   Initialized Zevio GPU instance.
// s:   Target tiled surface.
// fmt: Texture encoding format.
// sx:  Source atlas X coordinate.
// sy:  Source atlas Y coordinate.
// w:   Sprite width in pixels.
// h:   Sprite height in pixels.
// dx:  Destination X coordinate.
// dy:  Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_sprite_ex(vflash_gpu_t *g,
                                        vflash_gpu_surface_t *s,
                                        vflash_gpu_texture_format_t fmt,
                                        int32_t sx, int32_t sy,
                                        int32_t w, int32_t h,
                                        int32_t dx, int32_t dy);

//
// Submit a Zevio 3D orthographic textured quad draw command.
//
// g:       Initialized Zevio GPU instance.
// s:       Target tiled surface.
// fmt:     Texture encoding format.
// sx:      Source atlas origin X coordinate.
// sy:      Source atlas origin Y coordinate.
// log2_tw: Texture width exponent (0..7).
// log2_th: Texture height exponent (0..7).
// verts:   Four quad corners in Q16.16 fixed-point.
// alpha:   Global opacity (0..255).
// rgb_mod: 24-bit RGB color modulation (0x00RRGGBB).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_quad_tex(
    vflash_gpu_t *g, vflash_gpu_surface_t *s,
    vflash_gpu_texture_format_t fmt, int32_t sx, int32_t sy,
    uint8_t log2_tw, uint8_t log2_th,
    const vflash_gpu_quad_vertex_t verts[4],
    uint8_t alpha, uint32_t rgb_mod);

//
// Submit a Zevio 3D orthographic textured triangle draw command.
//
// g:       Initialized Zevio GPU instance.
// s:       Target tiled surface.
// fmt:     Texture encoding format.
// sx:      Source atlas origin X coordinate.
// sy:      Source atlas origin Y coordinate.
// log2_tw: Texture width exponent (0..7).
// log2_th: Texture height exponent (0..7).
// verts:   Three triangle vertices in Q16.16 fixed-point.
// alpha:   Global opacity (0..255).
// rgb_mod: 24-bit RGB color modulation (0x00RRGGBB).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t gpu_zevio_tri_tex(
    vflash_gpu_t *g, vflash_gpu_surface_t *s,
    vflash_gpu_texture_format_t fmt, int32_t sx, int32_t sy,
    uint8_t log2_tw, uint8_t log2_th,
    const vflash_gpu_quad_vertex_t verts[3],
    uint8_t alpha, uint32_t rgb_mod);

#endif // VFLASH_GPU_INTERNAL_H
