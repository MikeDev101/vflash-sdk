#ifndef VFLASH_GPU_H
#define VFLASH_GPU_H
#include "vflash/framebuffer.h"
typedef enum { VFLASH_GPU_SOFTWARE, VFLASH_GPU_ZEVIO } vflash_gpu_backend_t;
typedef enum { VFLASH_GPU_OK, VFLASH_GPU_UNSUPPORTED, VFLASH_GPU_INVALID, VFLASH_GPU_NOT_INITIALIZED, VFLASH_GPU_BUSY, VFLASH_GPU_TIMEOUT, VFLASH_GPU_DEVICE_ERROR } vflash_gpu_result_t;
typedef enum { VFLASH_GPU_LAYOUT_LINEAR, VFLASH_GPU_LAYOUT_ZEVIO_TILED } vflash_gpu_layout_t;
typedef struct { uint32_t initialized, backend; } vflash_gpu_t;
typedef struct { uint32_t hardware_accelerated, fill_rect, blit, alpha_blend, scaling, rotation, textured_triangles, present, same_surface_copy, upload; } vflash_gpu_caps_t;
/* Caller-owned storage; stride/capacity are in pixels. Physical address is
 * metadata supplied by the caller, never inferred from a CPU virtual pointer. */
typedef struct {
    vflash_framebuffer_t fb;
    uint32_t physical_address, physical_known;
    int32_t clip_x, clip_y, clip_w, clip_h;
    vflash_gpu_layout_t layout; /* software registration initializes LINEAR */
} vflash_gpu_surface_t;
typedef struct { uint32_t lists, submission_ticks, completion_ticks; } vflash_gpu_stats_t;
vflash_gpu_result_t vflash_gpu_stats(vflash_gpu_t *gpu,vflash_gpu_stats_t *stats);
vflash_gpu_result_t vflash_gpu_init(vflash_gpu_t *gpu, vflash_gpu_backend_t backend);
vflash_gpu_caps_t vflash_gpu_caps(const vflash_gpu_t *gpu);
vflash_gpu_result_t vflash_gpu_shutdown(vflash_gpu_t *gpu);
vflash_gpu_caps_t vflash_gpu_caps_for_layout(const vflash_gpu_t *gpu,vflash_gpu_layout_t layout);
/* Experimental native subset: tile-aligned 32x8..1024x1024 view or legacy 64x16,
 * 16384 bytes per 8-row band, 64-byte alignment, identity CPU address, caches off.
 * Caller owns lifetime. See docs/reference/gpu-native-hal.md before enabling ZEVIO. */
vflash_gpu_result_t vflash_gpu_surface_register_tiled_ex(vflash_gpu_surface_t *surface,void *pixels,uint32_t physical_address,uint32_t width,uint32_t height,uint32_t bytes);
vflash_gpu_result_t vflash_gpu_surface_register_tiled(vflash_gpu_surface_t *surface,void *pixels,uint32_t physical_address,uint32_t bytes);
vflash_gpu_result_t vflash_gpu_present(vflash_gpu_t *gpu,vflash_gpu_surface_t *surface,int32_t x,int32_t y);
vflash_gpu_result_t vflash_gpu_copy_region(vflash_gpu_t *gpu,vflash_gpu_surface_t *surface,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy);
vflash_gpu_result_t vflash_gpu_upload(vflash_gpu_t *gpu,vflash_gpu_surface_t *dst,const vflash_gpu_surface_t *src,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy);
vflash_gpu_result_t vflash_gpu_surface_register(vflash_gpu_surface_t *surface, const vflash_framebuffer_t *fb,
    uint32_t physical_address, uint32_t physical_known);
vflash_gpu_result_t vflash_gpu_clip(vflash_gpu_surface_t *surface, int32_t x, int32_t y, int32_t w, int32_t h);
vflash_gpu_result_t vflash_gpu_fill_rect(vflash_gpu_t *gpu,vflash_gpu_surface_t *surface,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb);
vflash_gpu_result_t vflash_gpu_blit(vflash_gpu_t *gpu,vflash_gpu_surface_t *dst,const vflash_gpu_surface_t *src,int32_t x,int32_t y);
vflash_gpu_result_t vflash_gpu_wait_idle(vflash_gpu_t *gpu);
#endif
