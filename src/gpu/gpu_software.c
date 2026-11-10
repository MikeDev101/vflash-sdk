#include "gpu_internal.h"
#include "vflash/draw.h"
static int64_t max64(int64_t a,int64_t b) {return a>b?a:b;}
static int64_t min64(int64_t a,int64_t b) {return a<b?a:b;}
vflash_gpu_result_t gpu_software_fill(vflash_gpu_surface_t *s,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb)
{
    if (!gpu_surface_valid(s) || w<0 || h<0) return VFLASH_GPU_INVALID;
    int64_t l=max64(x,s->clip_x),t=max64(y,s->clip_y);
    int64_t r=min64((int64_t)x+w,(int64_t)s->clip_x+s->clip_w);
    int64_t b=min64((int64_t)y+h,(int64_t)s->clip_y+s->clip_h);
    if (l<r && t<b) draw_rect(&s->fb,(int32_t)l,(int32_t)t,(int32_t)(r-l),(int32_t)(b-t),rgb);
    return VFLASH_GPU_OK;
}
vflash_gpu_result_t gpu_software_blit(vflash_gpu_surface_t *d,const vflash_gpu_surface_t *s,int32_t x,int32_t y)
{
    if (!gpu_surface_valid(d) || !gpu_surface_valid(s)) return VFLASH_GPU_INVALID;
    uintptr_t db=(uintptr_t)d->fb.pixels,sb=(uintptr_t)s->fb.pixels;
    uintptr_t de=db+(uintptr_t)d->fb.capacity_pixels*2,se=sb+(uintptr_t)s->fb.capacity_pixels*2;
    int overlap=db<se && sb<de;
    /* Exact-base views with equal stride support memmove semantics. Other
     * overlapping registrations are rejected before any writes. */
    if (overlap && (db!=sb || d->fb.stride!=s->fb.stride)) return VFLASH_GPU_UNSUPPORTED;
    int64_t l=max64(x,d->clip_x),t=max64(y,d->clip_y);
    int64_t r=min64((int64_t)x+s->fb.width,(int64_t)d->clip_x+d->clip_w);
    int64_t b=min64((int64_t)y+s->fb.height,(int64_t)d->clip_y+d->clip_h);
    if (l>=r || t>=b) return VFLASH_GPU_OK;
    int reverse=overlap && (y>0 || (y==0 && x>0));
    for (int64_t row=0;row<b-t;++row) {
        int64_t dy=reverse?b-1-row:t+row;
        for (int64_t col=0;col<r-l;++col) {
            int64_t dx=reverse?r-1-col:l+col;
            d->fb.pixels[(uint32_t)dy*d->fb.stride+(uint32_t)dx]=
                s->fb.pixels[(uint32_t)(dy-y)*s->fb.stride+(uint32_t)(dx-x)];
        }
    }
    return VFLASH_GPU_OK;
}
