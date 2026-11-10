#ifndef VFLASH_VIDEO_H
#define VFLASH_VIDEO_H
#include "vflash/framebuffer.h"
int vflash_video_init(vflash_framebuffer_t *fb);
int vflash_video_present(const vflash_framebuffer_t *fb);
int vflash_video_backbuffer(vflash_framebuffer_t *fb);
uint32_t vflash_video_front_bank(void);
/* Experimental fixed-size 64x16 native view with checked placement; called by GPU HAL. */
int vflash_video_present_tiled(uint32_t physical_address,int32_t x,int32_t y);
int vflash_video_restore_linear(void);
uint32_t vflash_video_tiled_address(void);
#endif
