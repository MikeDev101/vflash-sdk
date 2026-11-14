#ifndef VFLASH_GPU_H
#define VFLASH_GPU_H

#include "vflash/framebuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// Available GPU HAL rendering backends.
//
typedef enum
{
    VFLASH_GPU_SOFTWARE,
    VFLASH_GPU_ZEVIO
} vflash_gpu_backend_t;

//
// Status codes returned by GPU HAL functions.
//
typedef enum
{
    VFLASH_GPU_OK,
    VFLASH_GPU_UNSUPPORTED,
    VFLASH_GPU_INVALID,
    VFLASH_GPU_NOT_INITIALIZED,
    VFLASH_GPU_BUSY,
    VFLASH_GPU_TIMEOUT,
    VFLASH_GPU_DEVICE_ERROR,
    VFLASH_GPU_OUT_OF_MEMORY
} vflash_gpu_result_t;

//
// Pixel memory layout of a registered GPU surface.
//
typedef enum
{
    VFLASH_GPU_LAYOUT_LINEAR,
    VFLASH_GPU_LAYOUT_ZEVIO_TILED
} vflash_gpu_layout_t;

//
// Hardware sprite/texture pixel encoding formats.
//
typedef enum
{
    VFLASH_GPU_TEXTURE_BGR555_KEY,
    VFLASH_GPU_TEXTURE_ABGR4444
} vflash_gpu_texture_format_t;

//
// GPU HAL instance state.
//
typedef struct
{
    uint32_t initialized;
    uint32_t backend;
} vflash_gpu_t;

//
// Capability flags reported for a GPU backend and surface layout.
//
typedef struct
{
    uint32_t hardware_accelerated;
    uint32_t fill_rect;
    uint32_t blit;
    uint32_t alpha_blend;
    uint32_t scaling;
    uint32_t rotation;
    uint32_t textured_triangles;
    uint32_t present;
    uint32_t same_surface_copy;
    uint32_t upload;
    uint32_t color_key_sprite;
} vflash_gpu_caps_t;

//
// GPU surface descriptor wrapping caller-owned pixel storage.
// Stride and capacity are in pixels; physical_address is caller-supplied
// metadata and is never inferred from a CPU virtual pointer.
//
typedef struct
{
    vflash_framebuffer_t fb;
    uint32_t physical_address;
    uint32_t physical_known;
    int32_t clip_x;
    int32_t clip_y;
    int32_t clip_w;
    int32_t clip_h;
    vflash_gpu_layout_t layout;
} vflash_gpu_surface_t;

//
// Cumulative GPU command list submission and timing counters.
//
typedef struct
{
    uint32_t lists;
    uint32_t submission_ticks;
    uint32_t completion_ticks;
} vflash_gpu_stats_t;

//
// Query cumulative GPU command list and timing statistics.
//
// gpu:   Initialized GPU instance.
// stats: Output statistics structure.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_stats(vflash_gpu_t *gpu,
                                     vflash_gpu_stats_t *stats);

//
// Initialize the GPU HAL using the selected backend.
//
// gpu:     GPU instance to initialize.
// backend: Target backend (VFLASH_GPU_SOFTWARE or VFLASH_GPU_ZEVIO).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_init(vflash_gpu_t *gpu,
                                    vflash_gpu_backend_t backend);

//
// Query default capabilities for the initialized GPU backend.
//
// gpu: GPU instance to query.
//
// Returns a vflash_gpu_caps_t capability structure.
//
vflash_gpu_caps_t vflash_gpu_caps(const vflash_gpu_t *gpu);

//
// Shut down the GPU HAL and release backend state.
//
// gpu: GPU instance to shut down.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_shutdown(vflash_gpu_t *gpu);

//
// Query GPU capabilities for a specific surface memory layout.
//
// gpu:    GPU instance to query.
// layout: Surface layout (linear or Zevio tiled).
//
// Returns a vflash_gpu_caps_t capability structure.
//
vflash_gpu_caps_t vflash_gpu_caps_for_layout(const vflash_gpu_t *gpu,
                                             vflash_gpu_layout_t layout);

//
// Register a Zevio tiled surface with explicit dimensions (32x8..1024x1024,
// 16384 bytes per 8-row band, 64-byte alignment, identity CPU address).
//
// surface:          Output surface descriptor.
// pixels:           Caller-owned 64-byte aligned backing storage.
// physical_address: Physical SDRAM address of pixels.
// width:            Surface width in pixels.
// height:           Surface height in pixels.
// bytes:            Total backing buffer size in bytes.
//
// Returns VFLASH_GPU_OK on success, or VFLASH_GPU_INVALID on bad arguments.
//
vflash_gpu_result_t vflash_gpu_surface_register_tiled_ex(
    vflash_gpu_surface_t *surface, void *pixels, uint32_t physical_address,
    uint32_t width, uint32_t height, uint32_t bytes);

//
// Register a legacy 64x16 Zevio tiled surface.
//
// surface:          Output surface descriptor.
// pixels:           Caller-owned 64-byte aligned backing storage.
// physical_address: Physical SDRAM address of pixels.
// bytes:            Total backing buffer size in bytes.
//
// Returns VFLASH_GPU_OK on success, or VFLASH_GPU_INVALID on bad arguments.
//
vflash_gpu_result_t vflash_gpu_surface_register_tiled(
    vflash_gpu_surface_t *surface, void *pixels, uint32_t physical_address,
    uint32_t bytes);

//
// Present a sub-view of a tiled surface at screen coordinates (x, y).
//
// gpu:     Initialized GPU instance.
// surface: Tiled surface to display.
// width:   Visible view width in pixels.
// height:  Visible view height in pixels.
// x:       Screen destination X coordinate.
// y:       Screen destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_present_view(vflash_gpu_t *gpu,
                                            vflash_gpu_surface_t *surface,
                                            uint32_t width, uint32_t height,
                                            int32_t x, int32_t y);

//
// Present an entire tiled surface at screen coordinates (x, y).
//
// gpu:     Initialized GPU instance.
// surface: Tiled surface to display.
// x:       Screen destination X coordinate.
// y:       Screen destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_present(vflash_gpu_t *gpu,
                                       vflash_gpu_surface_t *surface,
                                       int32_t x, int32_t y);

//
// Copy an opaque rectangular region within the same tiled surface.
//
// gpu:     Initialized GPU instance.
// surface: Target tiled surface.
// sx:      Source X coordinate.
// sy:      Source Y coordinate.
// w:       Region width in pixels.
// h:       Region height in pixels.
// dx:      Destination X coordinate.
// dy:      Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_copy_region(vflash_gpu_t *gpu,
                                           vflash_gpu_surface_t *surface,
                                           int32_t sx, int32_t sy,
                                           int32_t w, int32_t h,
                                           int32_t dx, int32_t dy);

//
// Upload pixels from a linear source surface into a tiled destination surface.
//
// gpu: Initialized GPU instance.
// dst: Destination tiled surface.
// src: Source linear surface.
// sx:  Source X coordinate.
// sy:  Source Y coordinate.
// w:   Region width in pixels.
// h:   Region height in pixels.
// dx:  Destination X coordinate.
// dy:  Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_upload(vflash_gpu_t *gpu,
                                      vflash_gpu_surface_t *dst,
                                      const vflash_gpu_surface_t *src,
                                      int32_t sx, int32_t sy,
                                      int32_t w, int32_t h,
                                      int32_t dx, int32_t dy);

//
// Draw a BGR555 color-keyed sprite region within a tiled surface.
//
// gpu:     Initialized GPU instance.
// surface: Target tiled surface containing both atlas and viewport.
// sx:      Source atlas X coordinate.
// sy:      Source atlas Y coordinate.
// w:       Sprite width in pixels.
// h:       Sprite height in pixels.
// dx:      Destination X coordinate.
// dy:      Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_draw_sprite(vflash_gpu_t *gpu,
                                           vflash_gpu_surface_t *surface,
                                           int32_t sx, int32_t sy,
                                           int32_t w, int32_t h,
                                           int32_t dx, int32_t dy);

//
// Draw a sprite region within a tiled surface using an explicit texture
// format (color-keyed BGR555 or translucent ABGR4444).
//
// gpu:     Initialized GPU instance.
// surface: Target tiled surface.
// format:  Texture encoding format.
// sx:      Source atlas X coordinate.
// sy:      Source atlas Y coordinate.
// w:       Sprite width in pixels.
// h:       Sprite height in pixels.
// dx:      Destination X coordinate.
// dy:      Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_draw_sprite_ex(
    vflash_gpu_t *gpu, vflash_gpu_surface_t *surface,
    vflash_gpu_texture_format_t format, int32_t sx, int32_t sy,
    int32_t w, int32_t h, int32_t dx, int32_t dy);

//
// 2D textured vertex in Q16.16 fixed-point coordinates.
//
// x: Screen X coordinate (Q16.16 fixed-point pixels).
// y: Screen Y coordinate (Q16.16 fixed-point pixels).
// u: Normalized texture U coordinate (Q16.16, 0x10000 = 1.0).
// v: Normalized texture V coordinate (Q16.16, 0x10000 = 1.0).
//
typedef struct {
    int32_t x;
    int32_t y;
    int32_t u;
    int32_t v;
} vflash_gpu_quad_vertex_t;

//
// Draw a hardware-accelerated textured quad via the Zevio 3D engine in
// orthographic screen space.
//
// gpu:     Initialized GPU instance.
// surface: Target tiled surface containing both atlas and viewport.
// format:  Texture encoding format (BGR555_KEY or ABGR4444).
// sx:      Source atlas origin X coordinate in pixels.
// sy:      Source atlas origin Y coordinate in pixels.
// log2_tw: Texture width exponent (width = 8 << log2_tw, range 0..7).
// log2_th: Texture height exponent (height = 8 << log2_th, range 0..7).
// verts:   Four quad corners in clockwise/CCW order (drawn as 0-1-2, 0-2-3).
// alpha:   Global opacity (0..255, 255 = opaque).
// rgb_mod: 24-bit RGB color modulation (0x00RRGGBB, 0x00FFFFFF = untinted).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_draw_quad_tex(
    vflash_gpu_t *gpu, vflash_gpu_surface_t *surface,
    vflash_gpu_texture_format_t format, int32_t sx, int32_t sy,
    uint8_t log2_tw, uint8_t log2_th,
    const vflash_gpu_quad_vertex_t verts[4],
    uint8_t alpha, uint32_t rgb_mod);

//
// Draw a hardware-accelerated textured triangle via the Zevio 3D engine in
// orthographic screen space.
//
// gpu:     Initialized GPU instance.
// surface: Target tiled surface containing both atlas and viewport.
// format:  Texture encoding format (BGR555_KEY or ABGR4444).
// sx:      Source atlas origin X coordinate in pixels.
// sy:      Source atlas origin Y coordinate in pixels.
// log2_tw: Texture width exponent (width = 8 << log2_tw, range 0..7).
// log2_th: Texture height exponent (height = 8 << log2_th, range 0..7).
// verts:   Three triangle vertices.
// alpha:   Global opacity (0..255, 255 = opaque).
// rgb_mod: 24-bit RGB color modulation (0x00RRGGBB, 0x00FFFFFF = untinted).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_draw_tri_tex(
    vflash_gpu_t *gpu, vflash_gpu_surface_t *surface,
    vflash_gpu_texture_format_t format, int32_t sx, int32_t sy,
    uint8_t log2_tw, uint8_t log2_th,
    const vflash_gpu_quad_vertex_t verts[3],
    uint8_t alpha, uint32_t rgb_mod);

//
// Register a linear framebuffer as a GPU surface.
//
// surface:          Output surface descriptor.
// fb:               Source linear framebuffer descriptor.
// physical_address: Caller-provided physical SDRAM address of fb->pixels.
// physical_known:   Non-zero if physical_address is valid.
//
// Returns VFLASH_GPU_OK on success, or VFLASH_GPU_INVALID on bad arguments.
//
vflash_gpu_result_t vflash_gpu_surface_register(vflash_gpu_surface_t *surface,
                                                const vflash_framebuffer_t *fb,
                                                uint32_t physical_address,
                                                uint32_t physical_known);

//
// Set the active clipping rectangle on a GPU surface.
//
// surface: Target GPU surface.
// x:       Clip rectangle left X coordinate.
// y:       Clip rectangle top Y coordinate.
// w:       Clip rectangle width in pixels.
// h:       Clip rectangle height in pixels.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_clip(vflash_gpu_surface_t *surface,
                                    int32_t x, int32_t y,
                                    int32_t w, int32_t h);

//
// Fill a clipped rectangle on a GPU surface with a 24-bit RGB color.
//
// gpu:     Initialized GPU instance.
// surface: Target GPU surface.
// x:       Rectangle left X coordinate.
// y:       Rectangle top Y coordinate.
// w:       Rectangle width in pixels.
// h:       Rectangle height in pixels.
// rgb:     24-bit fill color (0x00RRGGBB).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_fill_rect(vflash_gpu_t *gpu,
                                         vflash_gpu_surface_t *surface,
                                         int32_t x, int32_t y,
                                         int32_t w, int32_t h, uint32_t rgb);

//
// Blit an entire source surface onto a destination surface at (x, y).
//
// gpu: Initialized GPU instance.
// dst: Destination GPU surface.
// src: Source GPU surface.
// x:   Destination X coordinate.
// y:   Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_blit(vflash_gpu_t *gpu,
                                    vflash_gpu_surface_t *dst,
                                    const vflash_gpu_surface_t *src,
                                    int32_t x, int32_t y);

//
// Wait until any in-flight GPU command list completes.
//
// gpu: Initialized GPU instance.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_gpu_wait_idle(vflash_gpu_t *gpu);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_GPU_H
