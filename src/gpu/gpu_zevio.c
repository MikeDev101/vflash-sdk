#include "gpu_internal.h"
#if defined(__arm__)
#include "vflash/video.h"
#include "vflash/clock.h"
#include "vflash/draw.h"
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
static vflash_gpu_t *owner;
static uint32_t poisoned;
static uint32_t stat_lists, stat_submit, stat_wait;
int gpu_zevio_in_use(vflash_gpu_t *g) { return owner==g; }
vflash_gpu_result_t gpu_zevio_wait(vflash_gpu_t *g)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    return poisoned ? VFLASH_GPU_TIMEOUT:VFLASH_GPU_OK;
}
static uint32_t commands[16] __attribute__((aligned(64)));
static int cpu_safe(void)
{
    uint32_t c,s;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(c));
    __asm__ volatile("mrs %0,cpsr" : "=r"(s));
    return !(c&0x3004) && (s&255)==0xD3;
}
vflash_gpu_result_t gpu_zevio_init(vflash_gpu_t *g)
{
    if (owner) return VFLASH_GPU_BUSY;
    if (poisoned || !cpu_safe()) return VFLASH_GPU_UNSUPPORTED;
    if (REG(0x900B0024)!=0x100 || REG(0x90010004)!=3749 || REG(0x90010008)!=15)
        return VFLASH_GPU_DEVICE_ERROR;
    stat_lists=stat_submit=stat_wait=0;
    owner=g;return VFLASH_GPU_OK;
}
static vflash_gpu_result_t submit(vflash_gpu_t *g,vflash_gpu_surface_t *s,uint32_t n)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (poisoned) return VFLASH_GPU_TIMEOUT;
    if (!cpu_safe()) return VFLASH_GPU_UNSUPPORTED;
    if (!gpu_tiled_valid(s)) return VFLASH_GPU_INVALID;
    uint32_t front=vflash_video_tiled_address(),fbytes=vflash_video_tiled_bytes();
    uint32_t sbytes=(s->fb.height>>3)*16384u;
    if (front && s->physical_address<front+fbytes && front<s->physical_address+sbytes)
        return VFLASH_GPU_BUSY;
    commands[n]=0x1400FD01;
    REG(0xA8000014)=0x3F;REG(0xA8000090)=512;
    REG(0xA8000098)=s->physical_address;
    REG(0xA8000028)=(uint32_t)(uintptr_t)commands;
    __asm__ volatile("" ::: "memory");
    uint16_t stamp=vflash_clock_ticks();REG(0xA8000000)=1;
    stat_submit+=(uint16_t)(vflash_clock_ticks()-stamp);
    uint16_t start=vflash_clock_ticks();
    while (!(REG(0xA8000014)&8)) {
        if ((uint16_t)(vflash_clock_ticks()-start)>=1000) {
            poisoned=1;return VFLASH_GPU_TIMEOUT;
        }
    }
    stat_wait+=(uint16_t)(vflash_clock_ticks()-start);++stat_lists;
    REG(0xA8000014)=0x3F;
    if (REG(0xA8000014)&0x3F) {poisoned=1;return VFLASH_GPU_DEVICE_ERROR;}
    return VFLASH_GPU_OK;
}
vflash_gpu_result_t gpu_zevio_fill(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (poisoned) return VFLASH_GPU_TIMEOUT;
    if (!s || s->layout!=VFLASH_GPU_LAYOUT_ZEVIO_TILED) return VFLASH_GPU_UNSUPPORTED;
    if (!gpu_tiled_valid(s) || w<0 || h<0) return VFLASH_GPU_INVALID;
    int64_t l=x,t=y,r=(int64_t)x+w,b=(int64_t)y+h;
    if (l<s->clip_x) l=s->clip_x;
    if (t<s->clip_y) t=s->clip_y;
    if (r>(int64_t)s->clip_x+s->clip_w) r=(int64_t)s->clip_x+s->clip_w;
    if (b>(int64_t)s->clip_y+s->clip_h) b=(int64_t)s->clip_y+s->clip_h;
    if (l>=r || t>=b) return VFLASH_GPU_OK;
    commands[0]=0x96000000;commands[1]=0x82000000;
    commands[2]=0x83000000u|((s->fb.height-1u)<<12)|(s->fb.width-1u);
    commands[3]=0xD3000003;commands[4]=(uint32_t)t<<16|(uint32_t)l;
    commands[5]=(uint32_t)(b-t)<<16|(uint32_t)(r-l);commands[6]=draw_color(rgb);
    return submit(g,s,7);
}
vflash_gpu_result_t gpu_zevio_copy(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (poisoned) return VFLASH_GPU_TIMEOUT;
    if (!gpu_tiled_valid(s)) return VFLASH_GPU_INVALID;
    if (sx<0 || sy<0 || dx<0 || dy<0 || w<0 || h<0 ||
        (int64_t)sx+w>s->fb.width || (int64_t)dx+w>s->fb.width ||
        (int64_t)sy+h>s->fb.height || (int64_t)dy+h>s->fb.height) return VFLASH_GPU_INVALID;
    if (!w || !h || (sx==dx && sy==dy)) return VFLASH_GPU_OK;
    if ((int64_t)w*h>65536) return VFLASH_GPU_UNSUPPORTED;
    /* No implicit clipping or generalized overlap semantics. */
    if (s->clip_x || s->clip_y || s->clip_w!=(int32_t)s->fb.width || s->clip_h!=(int32_t)s->fb.height) return VFLASH_GPU_UNSUPPORTED;
    if (sx<dx+w && dx<sx+w && sy<dy+h && dy<sy+h) return VFLASH_GPU_UNSUPPORTED;
    commands[0]=0xD2000003;commands[1]=(uint32_t)sy<<16|(uint32_t)sx;
    commands[2]=(uint32_t)h<<16|(uint32_t)w;commands[3]=(uint32_t)dy<<16|(uint32_t)dx;
    return submit(g,s,4);
}
vflash_gpu_result_t gpu_zevio_sprite_ex(vflash_gpu_t *g,vflash_gpu_surface_t *s,vflash_gpu_texture_format_t fmt,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (poisoned) return VFLASH_GPU_TIMEOUT;
    if (fmt!=VFLASH_GPU_TEXTURE_BGR555_KEY && fmt!=VFLASH_GPU_TEXTURE_ABGR4444) return VFLASH_GPU_INVALID;
    if (!gpu_tiled_valid(s)) return VFLASH_GPU_INVALID;
    if (sx<0 || sy<0 || w<0 || h<0 || (int64_t)sx+w>s->fb.width || (int64_t)sy+h>s->fb.height) return VFLASH_GPU_INVALID;
    int64_t l=dx,t=dy,r=(int64_t)dx+w,b=(int64_t)dy+h;
    if (l<s->clip_x) l=s->clip_x;
    if (t<s->clip_y) t=s->clip_y;
    if (r>(int64_t)s->clip_x+s->clip_w) r=(int64_t)s->clip_x+s->clip_w;
    if (b>(int64_t)s->clip_y+s->clip_h) b=(int64_t)s->clip_y+s->clip_h;
    if (l>=r || t>=b) return VFLASH_GPU_OK;
    int32_t u0=(int32_t)(sx+(l-dx)),v0=(int32_t)(sy+(t-dy)),cw=(int32_t)(r-l),ch=(int32_t)(b-t);
    if (u0<l+cw && l<u0+cw && v0<t+ch && t<v0+ch) return VFLASH_GPU_UNSUPPORTED;
    commands[0]=0x81000000;
    commands[1]=0x82000000u|((uint32_t)t<<12)|(uint32_t)l;
    commands[2]=0x83000000u|((uint32_t)(b-1)<<12)|(uint32_t)(r-1);
    commands[3]=0x1E000000;commands[4]=0x88000000;
    commands[5]=(fmt==VFLASH_GPU_TEXTURE_ABGR4444) ? 0x8600013Fu : 0x8600003Fu;
    commands[6]=0x87000000;
    commands[7]=0xC8010004;commands[8]=0;commands[9]=((uint32_t)t<<16)|(uint32_t)l;
    commands[10]=((uint32_t)ch<<16)|(uint32_t)cw;commands[11]=((uint32_t)v0<<16)|(uint32_t)u0;
    commands[12]=0x95000000;
    return submit(g,s,13);
}
vflash_gpu_result_t gpu_zevio_upload(vflash_gpu_t *g,vflash_gpu_surface_t *d,const vflash_gpu_surface_t *s,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (poisoned) return VFLASH_GPU_TIMEOUT;
    if (!gpu_tiled_valid(d) || !gpu_surface_valid(s)) return VFLASH_GPU_INVALID;
    if (s->physical_known!=1 || (uintptr_t)s->fb.pixels!=s->physical_address || (s->physical_address&3)) return VFLASH_GPU_INVALID;
    uint32_t sbytes=s->fb.capacity_pixels*2u;
    if (s->physical_address<0x10000000 || s->physical_address>0x11000000-sbytes) return VFLASH_GPU_INVALID;
    if (sx<0 || sy<0 || dx<0 || dy<0 || w<0 || h<0 ||
        (int64_t)sx+w>s->fb.width || (int64_t)sy+h>s->fb.height ||
        (int64_t)dx+w>d->fb.width || (int64_t)dy+h>d->fb.height) return VFLASH_GPU_INVALID;
    if (!w || !h) return VFLASH_GPU_OK;
    if (w&1) return VFLASH_GPU_INVALID;
    uint32_t offset=((uint32_t)sy*s->fb.stride+(uint32_t)sx)*2u;
    if (offset&3) return VFLASH_GPU_INVALID;
    if (d->clip_x || d->clip_y || d->clip_w!=(int32_t)d->fb.width || d->clip_h!=(int32_t)d->fb.height) return VFLASH_GPU_UNSUPPORTED;
    uint32_t dbytes=(d->fb.height>>3)*16384u;
    if (d->physical_address<s->physical_address+sbytes && s->physical_address<d->physical_address+dbytes) return VFLASH_GPU_UNSUPPORTED;
    if (h>1 && (uint32_t)w!=s->fb.stride) return VFLASH_GPU_UNSUPPORTED;
    uint32_t words=((uint32_t)w*(uint32_t)h)>>1;
    if (words>65535u) return VFLASH_GPU_UNSUPPORTED;
    commands[0]=0xD0000002;commands[1]=(uint32_t)dy<<16|(uint32_t)dx;
    commands[2]=(uint32_t)h<<16|(uint32_t)w;commands[3]=0x0E000000u|words;
    commands[4]=s->physical_address+offset;commands[5]=0x89000000;commands[6]=0x95000000;
    return submit(g,d,7);
}
vflash_gpu_result_t gpu_zevio_present_view(vflash_gpu_t *g,vflash_gpu_surface_t *s,uint32_t w,uint32_t h,int32_t x,int32_t y)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (poisoned) return VFLASH_GPU_TIMEOUT;
    if (!cpu_safe()) return VFLASH_GPU_UNSUPPORTED;
    if (!gpu_tiled_valid(s)) return VFLASH_GPU_UNSUPPORTED;
    if (!w || !h || (w&31) || (h&7) || w>s->fb.width || h>s->fb.height || w>VFLASH_WIDTH || h>VFLASH_HEIGHT) return VFLASH_GPU_INVALID;
    if (x<0 || y<0 || (int64_t)x+w>VFLASH_WIDTH || (int64_t)y+h>VFLASH_HEIGHT) return VFLASH_GPU_INVALID;
    return vflash_video_present_tiled_view(s->physical_address,(s->fb.height>>3)*16384u,w,h,x,y) ? VFLASH_GPU_OK:VFLASH_GPU_DEVICE_ERROR;
}
vflash_gpu_result_t gpu_zevio_present(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y)
{
    if (!s) return VFLASH_GPU_UNSUPPORTED;
    return gpu_zevio_present_view(g,s,s->fb.width,s->fb.height,x,y);
}
vflash_gpu_result_t gpu_zevio_stats(vflash_gpu_t *g,vflash_gpu_stats_t *s)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (!s) return VFLASH_GPU_INVALID;
    s->lists=stat_lists;s->submission_ticks=stat_submit;s->completion_ticks=stat_wait;
    return VFLASH_GPU_OK;
}
vflash_gpu_result_t gpu_zevio_shutdown(vflash_gpu_t *g)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (poisoned) return VFLASH_GPU_TIMEOUT;
    if (!vflash_video_restore_linear()) return VFLASH_GPU_DEVICE_ERROR;
    owner=0;return VFLASH_GPU_OK;
}
#else
vflash_gpu_result_t gpu_zevio_stats(vflash_gpu_t *g,vflash_gpu_stats_t *s) {(void)g;(void)s;return VFLASH_GPU_UNSUPPORTED;}
vflash_gpu_result_t gpu_zevio_wait(vflash_gpu_t *g) {(void)g;return VFLASH_GPU_UNSUPPORTED;}
int gpu_zevio_in_use(vflash_gpu_t *g) {(void)g;return 0;}
vflash_gpu_result_t gpu_zevio_init(vflash_gpu_t *g) { (void)g;return VFLASH_GPU_UNSUPPORTED; }
vflash_gpu_result_t gpu_zevio_fill(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb)
{(void)g;(void)s;(void)x;(void)y;(void)w;(void)h;(void)rgb;return VFLASH_GPU_UNSUPPORTED;}
vflash_gpu_result_t gpu_zevio_copy(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y,int32_t w,int32_t h,int32_t dx,int32_t dy)
{(void)g;(void)s;(void)x;(void)y;(void)w;(void)h;(void)dx;(void)dy;return VFLASH_GPU_UNSUPPORTED;}
vflash_gpu_result_t gpu_zevio_sprite_ex(vflash_gpu_t *g,vflash_gpu_surface_t *s,vflash_gpu_texture_format_t fmt,int32_t x,int32_t y,int32_t w,int32_t h,int32_t dx,int32_t dy)
{(void)g;(void)s;(void)fmt;(void)x;(void)y;(void)w;(void)h;(void)dx;(void)dy;return VFLASH_GPU_UNSUPPORTED;}
vflash_gpu_result_t gpu_zevio_upload(vflash_gpu_t *g,vflash_gpu_surface_t *d,const vflash_gpu_surface_t *s,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy)
{(void)g;(void)d;(void)s;(void)sx;(void)sy;(void)w;(void)h;(void)dx;(void)dy;return VFLASH_GPU_UNSUPPORTED;}
vflash_gpu_result_t gpu_zevio_present_view(vflash_gpu_t *g,vflash_gpu_surface_t *s,uint32_t w,uint32_t h,int32_t x,int32_t y)
{(void)g;(void)s;(void)w;(void)h;(void)x;(void)y;return VFLASH_GPU_UNSUPPORTED;}
vflash_gpu_result_t gpu_zevio_present(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y) {(void)g;(void)s;(void)x;(void)y;return VFLASH_GPU_UNSUPPORTED;}
vflash_gpu_result_t gpu_zevio_shutdown(vflash_gpu_t *g) {(void)g;return VFLASH_GPU_UNSUPPORTED;}
#endif
