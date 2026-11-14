#ifndef VFLASH_RENDERER_H
#define VFLASH_RENDERER_H

#include "vflash/gpu.h"
#include "vflash/video.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// Texture blending/transparency mode for Zevio 2D rendering.
//
typedef enum
{
    VFLASH_TEXTURE_OPAQUE,     // D2 same-surface region copy
    VFLASH_TEXTURE_KEYED,      // C8 BGR555 color-keyed sprite
    VFLASH_TEXTURE_TRANSLUCENT // C8 ABGR4444 translucent sprite
} vflash_texture_type_t;

//
// Allocated texture handle inside the renderer's offscreen atlas.
//
typedef struct
{
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint32_t width;
    uint32_t height;
    vflash_texture_type_t type;
    uint32_t id;
} vflash_texture_t;

//
// Configuration for double-buffered Zevio 2D renderer surfaces and atlas.
//
typedef struct
{
    void *surface_a;           // Primary 64-byte aligned tiled buffer
    void *surface_b;           // Secondary 64-byte aligned tiled buffer
    uint32_t surface_bytes;    // Byte size of each surface buffer
    uint32_t surface_width;    // Total tiled width (multiple of 32)
    uint32_t surface_height;   // Total tiled height (multiple of 8)
    uint32_t playfield_width;  // Visible viewport width (multiple of 32)
    uint32_t playfield_height; // Visible viewport height (multiple of 8)
    int32_t screen_x;          // Destination screen X on 320x240 display
    int32_t screen_y;          // Destination screen Y on 320x240 display
    uint32_t atlas_x;          // Offscreen atlas start X
    uint32_t atlas_y;          // Offscreen atlas start Y
    uint32_t atlas_width;      // Atlas width within surface_width
    uint32_t atlas_height;     // Atlas height within surface_height
} vflash_renderer_config_t;

#define VFLASH_MAX_TARGET_OPS 256
#define VFLASH_MAX_ATLAS_FREE_RECTS 128

//
// Free rectangle entry in the offscreen texture atlas allocator.
//
typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} vflash_atlas_rect_t;

//
// Deferred GPU command recorded when drawing into an offscreen render target
// while the opposite double-buffer surface is actively scanned out.
//
typedef struct
{
    uint8_t op;
    uint8_t reserved;
    int16_t sx;
    int16_t sy;
    int16_t w;
    int16_t h;
    int16_t dx;
    int16_t dy;
    int16_t cx;
    int16_t cy;
    int16_t cw;
    int16_t ch;
    uint32_t rgb;
} vflash_target_op_t;

//
// Double-buffered Zevio 2D renderer state and atlas allocator.
//
typedef struct
{
    vflash_gpu_t gpu;
    vflash_gpu_surface_t surf_a;
    vflash_gpu_surface_t surf_b;
    vflash_framebuffer_t linear_fb;
    vflash_renderer_config_t config;
    vflash_gpu_surface_t *current;
    uint32_t frame_count;
    uint32_t atlas_free_count;
    vflash_atlas_rect_t atlas_free[VFLASH_MAX_ATLAS_FREE_RECTS];
    int32_t clip_x;
    int32_t clip_y;
    int32_t clip_w;
    int32_t clip_h;
    uint32_t initialized;
    uint32_t generation_id;
    uint32_t has_target;
    vflash_texture_t target;
    int32_t saved_clip_x;
    int32_t saved_clip_y;
    int32_t saved_clip_w;
    int32_t saved_clip_h;
    uint32_t target_op_count;
    uint8_t target_op_overflow;
    vflash_target_op_t target_ops[VFLASH_MAX_TARGET_OPS];
} vflash_renderer_t;

//
// Initialize the Zevio 2D renderer with double-buffered tiled surfaces.
//
// r:      Renderer instance to initialize.
// config: Surface and atlas layout configuration.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_init(vflash_renderer_t *r,
                                         const vflash_renderer_config_t *config);

//
// Shut down the Zevio 2D renderer and release GPU resources.
//
// r: Initialized renderer instance.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_shutdown(vflash_renderer_t *r);

//
// Reset the offscreen texture atlas allocator for a new scene.
//
// r: Initialized renderer instance.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_reset_atlas(vflash_renderer_t *r);

//
// Invalidate a texture handle by clearing its metadata.
//
// r:   Initialized renderer instance.
// tex: Texture handle to invalidate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_destroy_texture(vflash_renderer_t *r,
                                                    vflash_texture_t *tex);

//
// Clear the active playfield viewport to a 24-bit RGB color.
//
// r:   Initialized renderer instance.
// rgb: 24-bit fill color (0x00RRGGBB).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_clear(vflash_renderer_t *r, uint32_t rgb);

//
// Fill a clipped rectangle on the active playfield viewport.
//
// r:   Initialized renderer instance.
// x:   Rectangle left X coordinate.
// y:   Rectangle top Y coordinate.
// w:   Rectangle width in pixels.
// h:   Rectangle height in pixels.
// rgb: 24-bit fill color (0x00RRGGBB).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_fill_rect(vflash_renderer_t *r,
                                              int32_t x, int32_t y,
                                              int32_t w, int32_t h,
                                              uint32_t rgb);

//
// Set the playfield clipping rectangle for subsequent draw calls.
//
// r: Initialized renderer instance.
// x: Clip rectangle left X coordinate.
// y: Clip rectangle top Y coordinate.
// w: Clip rectangle width in pixels.
// h: Clip rectangle height in pixels.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_set_clip(vflash_renderer_t *r,
                                             int32_t x, int32_t y,
                                             int32_t w, int32_t h);

//
// Reset the clipping rectangle to the full playfield viewport.
//
// r: Initialized renderer instance.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_reset_clip(vflash_renderer_t *r);

//
// Allocate space in the offscreen atlas and upload texture pixels to both
// double-buffered surfaces.
//
// r:      Initialized renderer instance.
// tex:    Output texture handle.
// pixels: Source 16-bit pixel buffer matching type encoding.
// width:  Texture width in pixels.
// height: Texture height in pixels.
// type:   Texture blending/keying mode.
//
// Returns VFLASH_GPU_OK on success, or VFLASH_GPU_OUT_OF_MEMORY / error.
//
vflash_gpu_result_t vflash_renderer_upload_texture(vflash_renderer_t *r,
                                                   vflash_texture_t *tex,
                                                   const void *pixels,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   vflash_texture_type_t type);

//
// Re-upload pixel data into an existing texture's atlas slot in-place.
//
// r:      Initialized renderer instance.
// tex:    Existing uploaded texture handle with matching dimensions.
// pixels: Source 16-bit pixel buffer matching type encoding.
// width:  Texture width in pixels (must match tex->width).
// height: Texture height in pixels (must match tex->height).
// type:   Texture blending/keying mode.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_update_texture(
    vflash_renderer_t *r, vflash_texture_t *tex, const void *pixels,
    uint32_t width, uint32_t height, vflash_texture_type_t type);

//
// Draw a full texture onto the active playfield at (dx, dy).
//
// r:   Initialized renderer instance.
// tex: Uploaded texture handle.
// dx:  Destination X coordinate.
// dy:  Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_draw(vflash_renderer_t *r,
                                         const vflash_texture_t *tex,
                                         int32_t dx, int32_t dy);

//
// Draw a sub-rectangle of a texture onto the active playfield at (dx, dy).
//
// r:   Initialized renderer instance.
// tex: Uploaded texture handle.
// sx:  Source X offset inside texture.
// sy:  Source Y offset inside texture.
// w:   Sub-rectangle width in pixels.
// h:   Sub-rectangle height in pixels.
// dx:  Destination X coordinate.
// dy:  Destination Y coordinate.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_draw_sub(vflash_renderer_t *r,
                                             const vflash_texture_t *tex,
                                             int32_t sx, int32_t sy,
                                             int32_t w, int32_t h,
                                             int32_t dx, int32_t dy);

//
// Flip flags for hardware-accelerated transformed sprite drawing.
//
#define VFLASH_FLIP_NONE       0x00u
#define VFLASH_FLIP_HORIZONTAL 0x01u
#define VFLASH_FLIP_VERTICAL   0x02u

//
// Parameters for hardware-accelerated sprite scaling, rotation, flipping,
// and blending via vflash_renderer_draw_transformed().
//
// dx:          Destination top-left X coordinate (pixels).
// dy:          Destination top-left Y coordinate (pixels).
// dw:          Destination width in pixels (>= 0; 0 draws nothing).
// dh:          Destination height in pixels (>= 0; 0 draws nothing).
// pivot_x:     Rotation pivot X relative to destination rect (pixels).
// pivot_y:     Rotation pivot Y relative to destination rect (pixels).
// angle_deg16: Clockwise rotation angle in Q16.16 fixed-point degrees.
// flip:        Bitmask of VFLASH_FLIP_HORIZONTAL / VFLASH_FLIP_VERTICAL.
// alpha:       Global opacity (0..255, 255 = opaque).
// color_mod:   24-bit RGB color tint (0x00RRGGBB, 0x00FFFFFF = untinted).
//
typedef struct {
    int32_t dx;
    int32_t dy;
    int32_t dw;
    int32_t dh;
    int32_t pivot_x;
    int32_t pivot_y;
    int32_t angle_deg16;
    uint32_t flip;
    uint8_t alpha;
    uint32_t color_mod;
} vflash_sprite_transform_t;

//
// Draw a sub-rectangle of a texture with hardware-accelerated scaling,
// rotation, flipping, and alpha/color modulation.
//
// r:     Initialized renderer instance.
// tex:   Uploaded texture handle.
// sx:    Source X offset inside texture.
// sy:    Source Y offset inside texture.
// sw:    Source sub-rectangle width in pixels.
// sh:    Source sub-rectangle height in pixels.
// xform: Transformation and blending parameters.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_draw_sub_transformed(
    vflash_renderer_t *r, const vflash_texture_t *tex,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh,
    const vflash_sprite_transform_t *xform);

//
// Draw a full texture with hardware-accelerated scaling, rotation, flipping,
// and alpha/color modulation.
//
// r:     Initialized renderer instance.
// tex:   Uploaded texture handle.
// xform: Transformation and blending parameters.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_draw_transformed(
    vflash_renderer_t *r, const vflash_texture_t *tex,
    const vflash_sprite_transform_t *xform);

//
// Draw a textured triangle onto the active playfield or render target.
// Coordinates (x, y) are in Q16.16 pixels; (u, v) are Q16.16 normalized
// coordinates in [0, 0x10000] relative to the full texture dimensions.
//
// r:         Initialized renderer instance.
// tex:       Uploaded texture handle.
// verts:     Three triangle vertices.
// alpha:     Global opacity (0..255, 255 = opaque).
// color_mod: 24-bit RGB color tint (0x00RRGGBB, 0x00FFFFFF = untinted).
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_draw_tri_tex(
    vflash_renderer_t *r, const vflash_texture_t *tex,
    const vflash_gpu_quad_vertex_t verts[3],
    uint8_t alpha, uint32_t color_mod);

//
// Allocate an offscreen render-target texture in the renderer atlas and clear
// it to transparent/black.
//
// r:      Initialized renderer instance.
// tex:    Output texture handle for the render target.
// width:  Target width in pixels.
// height: Target height in pixels.
// type:   Texture blending/keying mode when compositing the target.
//
// Returns VFLASH_GPU_OK on success, or VFLASH_GPU_OUT_OF_MEMORY / error.
//
vflash_gpu_result_t vflash_renderer_create_target(vflash_renderer_t *r,
                                                 vflash_texture_t *tex,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 vflash_texture_type_t type);

//
// Select an offscreen render target (or NULL for the default playfield).
//
// r:      Initialized renderer instance.
// target: Render target texture handle, or NULL for the playfield.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_set_target(vflash_renderer_t *r,
                                              const vflash_texture_t *target);

//
// Present the current playfield buffer and swap to the next backbuffer.
//
// r: Initialized renderer instance.
//
// Returns VFLASH_GPU_OK on success, or an error code.
//
vflash_gpu_result_t vflash_renderer_present(vflash_renderer_t *r);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_RENDERER_H
