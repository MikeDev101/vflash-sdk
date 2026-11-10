#ifndef VFLASH_GPU_INTERNAL_H
#define VFLASH_GPU_INTERNAL_H
#include "vflash/gpu.h"
int gpu_surface_valid(const vflash_gpu_surface_t *s);
vflash_gpu_result_t gpu_software_fill(vflash_gpu_surface_t *s,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb);
vflash_gpu_result_t gpu_software_blit(vflash_gpu_surface_t *d,const vflash_gpu_surface_t *s,int32_t x,int32_t y);
int gpu_tiled_valid(const vflash_gpu_surface_t *s);
vflash_gpu_result_t gpu_zevio_wait(vflash_gpu_t *g);
vflash_gpu_result_t gpu_zevio_stats(vflash_gpu_t *g,vflash_gpu_stats_t *stats);
int gpu_zevio_in_use(vflash_gpu_t *g);
vflash_gpu_result_t gpu_zevio_init(vflash_gpu_t *g);
vflash_gpu_result_t gpu_zevio_shutdown(vflash_gpu_t *g);
vflash_gpu_result_t gpu_zevio_present_view(vflash_gpu_t *g,vflash_gpu_surface_t *s,uint32_t w,uint32_t h,int32_t x,int32_t y);
vflash_gpu_result_t gpu_zevio_present(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y);
vflash_gpu_result_t gpu_zevio_fill(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb);
vflash_gpu_result_t gpu_zevio_copy(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy);
vflash_gpu_result_t gpu_zevio_upload(vflash_gpu_t *g,vflash_gpu_surface_t *d,const vflash_gpu_surface_t *s,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy);
vflash_gpu_result_t gpu_zevio_sprite_ex(vflash_gpu_t *g,vflash_gpu_surface_t *s,vflash_gpu_texture_format_t fmt,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy);
#endif
