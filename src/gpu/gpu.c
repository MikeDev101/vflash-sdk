#include "gpu_internal.h"
#include "vflash/draw.h"
#include <stddef.h>
int gpu_surface_valid(const vflash_gpu_surface_t *s)
{
    if (!s || s->layout!=VFLASH_GPU_LAYOUT_LINEAR || !draw_valid(&s->fb) || ((uintptr_t)s->fb.pixels&1)) return 0;
    if ((uint64_t)s->fb.capacity_pixels*2 > UINTPTR_MAX-(uintptr_t)s->fb.pixels) return 0;
    return s->clip_x>=0 && s->clip_y>=0 && s->clip_w>=0 && s->clip_h>=0 &&
        (uint64_t)s->clip_x+s->clip_w<=s->fb.width && (uint64_t)s->clip_y+s->clip_h<=s->fb.height;
}
vflash_gpu_result_t vflash_gpu_init(vflash_gpu_t *g,vflash_gpu_backend_t b)
{
    if (!g) return VFLASH_GPU_INVALID;
    if (gpu_zevio_in_use(g)) return VFLASH_GPU_BUSY;
    g->initialized=0;g->backend=b;
    if (b==VFLASH_GPU_ZEVIO) {
        vflash_gpu_result_t result=gpu_zevio_init(g);
        if (!result) g->initialized=1;
        return result;
    }
    if (b!=VFLASH_GPU_SOFTWARE) return VFLASH_GPU_INVALID;
    g->initialized=1;return VFLASH_GPU_OK;
}
vflash_gpu_caps_t vflash_gpu_caps(const vflash_gpu_t *g)
{
    volatile vflash_gpu_caps_t c;
    c.hardware_accelerated=0;c.fill_rect=0;c.blit=0;c.alpha_blend=0;
    c.scaling=0;c.rotation=0;c.textured_triangles=0;c.present=0;c.same_surface_copy=0;c.upload=0;c.color_key_sprite=0;
    if (g && g->initialized && g->backend==VFLASH_GPU_SOFTWARE) c.fill_rect=c.blit=1;
    return c;
}
vflash_gpu_result_t vflash_gpu_shutdown(vflash_gpu_t *g)
{
    if (!g || !g->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (g->backend==VFLASH_GPU_ZEVIO) {
        vflash_gpu_result_t result=gpu_zevio_shutdown(g);
        if (result) return result;
    }
    g->initialized=0;return VFLASH_GPU_OK;
}
vflash_gpu_result_t vflash_gpu_surface_register(vflash_gpu_surface_t *s,const vflash_framebuffer_t *fb,uint32_t pa,uint32_t known)
{
    if (!s || !fb || known>1) return VFLASH_GPU_INVALID;
    vflash_gpu_surface_t candidate={*fb,pa,known,0,0,(int32_t)fb->width,(int32_t)fb->height,VFLASH_GPU_LAYOUT_LINEAR};
    if (!gpu_surface_valid(&candidate)) return VFLASH_GPU_INVALID;
    *s=candidate;return VFLASH_GPU_OK;
}
vflash_gpu_result_t vflash_gpu_clip(vflash_gpu_surface_t *s,int32_t x,int32_t y,int32_t w,int32_t h)
{
    if ((!gpu_surface_valid(s) && !gpu_tiled_valid(s)) || w<0 || h<0) return VFLASH_GPU_INVALID;
    int64_t l=x,t=y,r=(int64_t)x+w,b=(int64_t)y+h;
    if (l<0) l=0;
    if (t<0) t=0;
    if (l>s->fb.width) l=s->fb.width;
    if (t>s->fb.height) t=s->fb.height;
    if (r<l) r=l;
    if (b<t) b=t;
    if (r>s->fb.width) r=s->fb.width;
    if (b>s->fb.height) b=s->fb.height;
    s->clip_x=(int32_t)l;s->clip_y=(int32_t)t;s->clip_w=(int32_t)(r-l);s->clip_h=(int32_t)(b-t);
    return VFLASH_GPU_OK;
}
static vflash_gpu_result_t ready(const vflash_gpu_t *g)
{
    if (!g || !g->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    return (g->backend==VFLASH_GPU_SOFTWARE || g->backend==VFLASH_GPU_ZEVIO) ? VFLASH_GPU_OK:VFLASH_GPU_UNSUPPORTED;
}
vflash_gpu_result_t vflash_gpu_fill_rect(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb)
{
    vflash_gpu_result_t r=ready(g);
    if (r) return r;
    if (g->backend==VFLASH_GPU_ZEVIO) return gpu_zevio_fill(g,s,x,y,w,h,rgb);
    if (s && s->layout!=VFLASH_GPU_LAYOUT_LINEAR) return VFLASH_GPU_UNSUPPORTED;
    return gpu_software_fill(s,x,y,w,h,rgb);
}
vflash_gpu_result_t vflash_gpu_blit(vflash_gpu_t *g,vflash_gpu_surface_t *d,const vflash_gpu_surface_t *s,int32_t x,int32_t y)
{
    vflash_gpu_result_t r=ready(g);
    if (r) return r;
    if (g->backend!=VFLASH_GPU_SOFTWARE || (s && s->layout!=VFLASH_GPU_LAYOUT_LINEAR) || (d && d->layout!=VFLASH_GPU_LAYOUT_LINEAR)) return VFLASH_GPU_UNSUPPORTED;
    return gpu_software_blit(d,s,x,y);
}
vflash_gpu_result_t vflash_gpu_wait_idle(vflash_gpu_t *g)
{
    vflash_gpu_result_t result=ready(g);
    if (result) return result;
    return g->backend==VFLASH_GPU_ZEVIO ? gpu_zevio_wait(g):VFLASH_GPU_OK;
}

int gpu_tiled_valid(const vflash_gpu_surface_t *s)
{
    if (!s || s->layout!=VFLASH_GPU_LAYOUT_ZEVIO_TILED || s->physical_known!=1 ||
        (uintptr_t)s->fb.pixels!=s->physical_address || (s->physical_address&63)) return 0;
    uint32_t w=s->fb.width,h=s->fb.height;
    if (!w || !h || (w&31) || (h&7) || w>1024 || h>1024) return 0;
    uint32_t pixels=(h>>3)*8192u,bytes=pixels*2u;
    return s->physical_address>=0x10000000 && s->physical_address<=0x11000000-bytes &&
        s->fb.stride==1024 && s->fb.capacity_pixels==pixels && s->fb.pixel_format==VFLASH_BGR555 &&
        s->clip_x>=0 && s->clip_y>=0 && s->clip_w>=0 && s->clip_h>=0 &&
        (int64_t)s->clip_x+s->clip_w<=w && (int64_t)s->clip_y+s->clip_h<=h;
}
vflash_gpu_result_t vflash_gpu_surface_register_tiled_ex(vflash_gpu_surface_t *s,void *pixels,uint32_t pa,uint32_t w,uint32_t h,uint32_t bytes)
{
    if (!s || !w || !h || (w&31) || (h&7) || w>1024 || h>1024) return VFLASH_GPU_INVALID;
    uint32_t cap=(h>>3)*8192u;
    if (bytes<cap*2u) return VFLASH_GPU_INVALID;
    vflash_gpu_surface_t t={{pixels,w,h,1024,cap,VFLASH_BGR555},pa,1,0,0,(int32_t)w,(int32_t)h,VFLASH_GPU_LAYOUT_ZEVIO_TILED};
    if (!gpu_tiled_valid(&t)) return VFLASH_GPU_INVALID;
    *s=t;return VFLASH_GPU_OK;
}
vflash_gpu_result_t vflash_gpu_surface_register_tiled(vflash_gpu_surface_t *s,void *pixels,uint32_t pa,uint32_t bytes)
{
    return vflash_gpu_surface_register_tiled_ex(s,pixels,pa,64,16,bytes);
}
vflash_gpu_caps_t vflash_gpu_caps_for_layout(const vflash_gpu_t *g,vflash_gpu_layout_t layout)
{
    volatile vflash_gpu_caps_t c;
    c.hardware_accelerated=0;c.fill_rect=0;c.blit=0;c.alpha_blend=0;
    c.scaling=0;c.rotation=0;c.textured_triangles=0;c.present=0;c.same_surface_copy=0;c.upload=0;c.color_key_sprite=0;
    if (!g || !g->initialized) return c;
    if (g->backend==VFLASH_GPU_SOFTWARE && layout==VFLASH_GPU_LAYOUT_LINEAR) return vflash_gpu_caps(g);
    if (g->backend==VFLASH_GPU_ZEVIO && layout==VFLASH_GPU_LAYOUT_ZEVIO_TILED)
        c.hardware_accelerated=c.fill_rect=c.present=c.same_surface_copy=c.upload=c.color_key_sprite=1;
    return c;
}
vflash_gpu_result_t vflash_gpu_present_view(vflash_gpu_t *g,vflash_gpu_surface_t *s,uint32_t w,uint32_t h,int32_t x,int32_t y)
{
    vflash_gpu_result_t r=ready(g);if (r) return r;
    return g->backend==VFLASH_GPU_ZEVIO ? gpu_zevio_present_view(g,s,w,h,x,y):VFLASH_GPU_UNSUPPORTED;
}
vflash_gpu_result_t vflash_gpu_present(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t x,int32_t y)
{
    vflash_gpu_result_t r=ready(g);if (r) return r;
    return g->backend==VFLASH_GPU_ZEVIO ? gpu_zevio_present(g,s,x,y):VFLASH_GPU_UNSUPPORTED;
}
vflash_gpu_result_t vflash_gpu_copy_region(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy)
{
    vflash_gpu_result_t r=ready(g);if (r) return r;
    if (s && s->layout!=VFLASH_GPU_LAYOUT_ZEVIO_TILED) return VFLASH_GPU_UNSUPPORTED;
    return g->backend==VFLASH_GPU_ZEVIO ? gpu_zevio_copy(g,s,sx,sy,w,h,dx,dy):VFLASH_GPU_UNSUPPORTED;
}
vflash_gpu_result_t vflash_gpu_draw_sprite(vflash_gpu_t *g,vflash_gpu_surface_t *s,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy)
{
    vflash_gpu_result_t r=ready(g);if (r) return r;
    if (s && s->layout!=VFLASH_GPU_LAYOUT_ZEVIO_TILED) return VFLASH_GPU_UNSUPPORTED;
    return g->backend==VFLASH_GPU_ZEVIO ? gpu_zevio_sprite(g,s,sx,sy,w,h,dx,dy):VFLASH_GPU_UNSUPPORTED;
}
vflash_gpu_result_t vflash_gpu_upload(vflash_gpu_t *g,vflash_gpu_surface_t *d,const vflash_gpu_surface_t *s,int32_t sx,int32_t sy,int32_t w,int32_t h,int32_t dx,int32_t dy)
{
    vflash_gpu_result_t r=ready(g);if (r) return r;
    if ((d && d->layout!=VFLASH_GPU_LAYOUT_ZEVIO_TILED) || (s && s->layout!=VFLASH_GPU_LAYOUT_LINEAR)) return VFLASH_GPU_UNSUPPORTED;
    return g->backend==VFLASH_GPU_ZEVIO ? gpu_zevio_upload(g,d,s,sx,sy,w,h,dx,dy):VFLASH_GPU_UNSUPPORTED;
}

vflash_gpu_result_t vflash_gpu_stats(vflash_gpu_t *g,vflash_gpu_stats_t *s)
{
    vflash_gpu_result_t r=ready(g);if (r) return r;
    if (!s) return VFLASH_GPU_INVALID;
    return g->backend==VFLASH_GPU_ZEVIO ? gpu_zevio_stats(g,s):VFLASH_GPU_UNSUPPORTED;
}
