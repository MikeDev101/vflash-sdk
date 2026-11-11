#ifndef VFLASH_VIDEO_H
#define VFLASH_VIDEO_H
#include "vflash/framebuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

int vflash_video_init(vflash_framebuffer_t *fb);
int vflash_video_present(const vflash_framebuffer_t *fb);
int vflash_video_backbuffer(vflash_framebuffer_t *fb);
uint32_t vflash_video_front_bank(void);
/* Experimental native tiled view with checked placement; called by GPU HAL. */
int vflash_video_present_tiled_view(uint32_t physical_address,uint32_t backing_bytes,uint32_t width,uint32_t height,int32_t x,int32_t y);
int vflash_video_present_tiled_ex(uint32_t physical_address,uint32_t width,uint32_t height,int32_t x,int32_t y);
int vflash_video_present_tiled(uint32_t physical_address,int32_t x,int32_t y);
int vflash_video_restore_linear(void);
uint32_t vflash_video_tiled_address(void);
uint32_t vflash_video_tiled_bytes(void);

#ifdef __cplusplus
}
#endif

#endif
