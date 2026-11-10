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
    uint32_t front=vflash_video_tiled_address();
    if (front && s->physical_address<front+32768 && front<s->physical_address+32768)
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
    commands[0]=0x96000000;commands[1]=0x82000000;commands[2]=0x8300F03F;
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
        (int64_t)sx+w>64 || (int64_t)dx+w>64 || (int64_t)sy+h>16 || (int64_t)dy+h>16) return VFLASH_GPU_INVALID;
    if (!w || !h || (sx==dx && sy==dy)) return VFLASH_GPU_OK;
    /* No implicit clipping or generalized overlap semantics. */
    if (s->clip_x || s->clip_y || s->clip_w!=64 || s->clip_h!=16) return VFLASH_GPU_UNSUPPORTED;
    if (sx<dx+w && dx<sx+w && sy<dy+h && dy<sy+h) return VFLASH_GPU_UNSUPPORTED;
    commands[0]=0xD2000003;commands[1]=(uint32_t)sy<<16|(uint32_t)sx;
    commands[2]=(uint32_t)h<<16|(uint32_t)w;commands[3]=(uint32_t)dy<<16|(uint32_t)dx;
    return submit(g,s,4);
}
vflash_gpu_result_t gpu_zevio_present(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y)
{
    if (owner!=g) return VFLASH_GPU_NOT_INITIALIZED;
    if (poisoned) return VFLASH_GPU_TIMEOUT;
    if (!cpu_safe()) return VFLASH_GPU_UNSUPPORTED;
    if (!gpu_tiled_valid(s)) return VFLASH_GPU_UNSUPPORTED;
    if (x<0 || y<0 || x>256 || y>224) return VFLASH_GPU_INVALID;
    return vflash_video_present_tiled(s->physical_address,x,y) ? VFLASH_GPU_OK:VFLASH_GPU_DEVICE_ERROR;
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
vflash_gpu_result_t gpu_zevio_present(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y) {(void)g;(void)s;(void)x;(void)y;return VFLASH_GPU_UNSUPPORTED;}
vflash_gpu_result_t gpu_zevio_shutdown(vflash_gpu_t *g) {(void)g;return VFLASH_GPU_UNSUPPORTED;}
#endif
