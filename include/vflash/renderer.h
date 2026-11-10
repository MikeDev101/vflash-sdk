#ifndef VFLASH_RENDERER_H
#define VFLASH_RENDERER_H

#include "vflash/gpu.h"
#include "vflash/video.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VFLASH_TEXTURE_OPAQUE,       /* D2 same-surface region copy */
    VFLASH_TEXTURE_KEYED,        /* C8 BGR555 color-keyed sprite */
    VFLASH_TEXTURE_TRANSLUCENT   /* C8 ABGR4444 translucent sprite */
} vflash_texture_type_t;

typedef struct {
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint32_t width;
    uint32_t height;
    vflash_texture_type_t type;
} vflash_texture_t;

typedef struct {
    void *surface_a;
    void *surface_b;
    uint32_t surface_bytes;
    uint32_t surface_width;      /* Total tiled surface width (e.g. 128, multiple of 32) */
    uint32_t surface_height;     /* Total tiled surface height (e.g. 32, multiple of 8) */
    uint32_t playfield_width;    /* Visible viewport width (multiple of 32) */
    uint32_t playfield_height;   /* Visible viewport height (multiple of 8) */
    int32_t screen_x;            /* Destination placement X on 320x240 display */
    int32_t screen_y;            /* Destination placement Y on 320x240 display */
    uint32_t atlas_x;            /* Start X of offscreen atlas region (typically >= playfield_width) */
    uint32_t atlas_y;            /* Start Y of offscreen atlas region */
    uint32_t atlas_width;        /* Atlas width (must fit inside surface_width) */
    uint32_t atlas_height;       /* Atlas height (must fit inside surface_height) */
} vflash_renderer_config_t;

typedef struct {
    vflash_gpu_t gpu;
    vflash_gpu_surface_t surf_a;
    vflash_gpu_surface_t surf_b;
    vflash_framebuffer_t linear_fb;
    vflash_renderer_config_t config;
    vflash_gpu_surface_t *current;
    uint32_t frame_count;
    uint32_t alloc_x;
    uint32_t alloc_y;
    uint32_t alloc_row_h;
    int32_t clip_x, clip_y, clip_w, clip_h;
    uint32_t initialized;
} vflash_renderer_t;

vflash_gpu_result_t vflash_renderer_init(vflash_renderer_t *r, const vflash_renderer_config_t *config);
vflash_gpu_result_t vflash_renderer_shutdown(vflash_renderer_t *r);

vflash_gpu_result_t vflash_renderer_clear(vflash_renderer_t *r, uint32_t rgb);
vflash_gpu_result_t vflash_renderer_fill_rect(vflash_renderer_t *r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);

vflash_gpu_result_t vflash_renderer_set_clip(vflash_renderer_t *r, int32_t x, int32_t y, int32_t w, int32_t h);
vflash_gpu_result_t vflash_renderer_reset_clip(vflash_renderer_t *r);

vflash_gpu_result_t vflash_renderer_upload_texture(vflash_renderer_t *r, vflash_texture_t *tex,
                                                   const void *pixels, uint32_t width, uint32_t height,
                                                   vflash_texture_type_t type);

vflash_gpu_result_t vflash_renderer_draw(vflash_renderer_t *r, const vflash_texture_t *tex, int32_t dx, int32_t dy);
vflash_gpu_result_t vflash_renderer_draw_sub(vflash_renderer_t *r, const vflash_texture_t *tex,
                                             int32_t sx, int32_t sy, int32_t w, int32_t h,
                                             int32_t dx, int32_t dy);

vflash_gpu_result_t vflash_renderer_present(vflash_renderer_t *r);

#ifdef __cplusplus
}
#endif

#endif

