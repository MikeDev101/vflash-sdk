#ifndef VFLASH_VIDEO_H
#define VFLASH_VIDEO_H
#include "vflash/framebuffer.h"
int vflash_video_init(vflash_framebuffer_t *fb);
int vflash_video_present(const vflash_framebuffer_t *fb);
int vflash_video_backbuffer(vflash_framebuffer_t *fb);
uint32_t vflash_video_front_bank(void);
#endif
