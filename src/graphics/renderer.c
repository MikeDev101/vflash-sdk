#include "vflash/renderer.h"
#include "vflash/draw.h"

vflash_gpu_result_t vflash_renderer_init(vflash_renderer_t *r, const vflash_renderer_config_t *cfg)
{
    if (!r || !cfg) return VFLASH_GPU_INVALID;
    if (!cfg->surface_a || !cfg->surface_b) return VFLASH_GPU_INVALID;
    if (((uintptr_t)cfg->surface_a & 63u) || ((uintptr_t)cfg->surface_b & 63u)) return VFLASH_GPU_INVALID;
    if (cfg->surface_width < 32 || (cfg->surface_width & 31u)) return VFLASH_GPU_INVALID;
    if (cfg->surface_height < 8 || (cfg->surface_height & 7u)) return VFLASH_GPU_INVALID;
    if (!cfg->playfield_width || (cfg->playfield_width & 31u) || cfg->playfield_width > cfg->surface_width) return VFLASH_GPU_INVALID;
    if (!cfg->playfield_height || (cfg->playfield_height & 7u) || cfg->playfield_height > cfg->surface_height) return VFLASH_GPU_INVALID;
    if (cfg->screen_x < 0 || cfg->screen_y < 0) return VFLASH_GPU_INVALID;
    if ((uint32_t)cfg->screen_x + cfg->playfield_width > VFLASH_WIDTH) return VFLASH_GPU_INVALID;
    if ((uint32_t)cfg->screen_y + cfg->playfield_height > VFLASH_HEIGHT) return VFLASH_GPU_INVALID;
    if (cfg->atlas_x + cfg->atlas_width > cfg->surface_width) return VFLASH_GPU_INVALID;
    if (cfg->atlas_y + cfg->atlas_height > cfg->surface_height) return VFLASH_GPU_INVALID;
    uint32_t min_bytes = (cfg->surface_height >> 3) * 16384u;
    if (cfg->surface_bytes < min_bytes) return VFLASH_GPU_INVALID;

    if (!vflash_video_init(&r->linear_fb)) return VFLASH_GPU_DEVICE_ERROR;
    draw_clear(&r->linear_fb, 0);
    if (!vflash_video_present(&r->linear_fb)) return VFLASH_GPU_DEVICE_ERROR;

    vflash_gpu_result_t res;
    res = vflash_gpu_surface_register_tiled_ex(&r->surf_a, cfg->surface_a, (uint32_t)(uintptr_t)cfg->surface_a,
                                               cfg->surface_width, cfg->surface_height, cfg->surface_bytes);
    if (res) return res;
    res = vflash_gpu_surface_register_tiled_ex(&r->surf_b, cfg->surface_b, (uint32_t)(uintptr_t)cfg->surface_b,
                                               cfg->surface_width, cfg->surface_height, cfg->surface_bytes);
    if (res) return res;

    res = vflash_gpu_init(&r->gpu, VFLASH_GPU_ZEVIO);
    if (res) return res;

    r->config = *cfg;
    r->current = &r->surf_a;
    r->frame_count = 0;
    r->alloc_x = cfg->atlas_x;
    r->alloc_y = cfg->atlas_y;
    r->alloc_row_h = 0;
    r->clip_x = 0;
    r->clip_y = 0;
    r->clip_w = (int32_t)cfg->playfield_width;
    r->clip_h = (int32_t)cfg->playfield_height;
    vflash_gpu_clip(&r->surf_a, 0, 0, r->clip_w, r->clip_h);
    vflash_gpu_clip(&r->surf_b, 0, 0, r->clip_w, r->clip_h);
    r->generation_id = 1;
    r->initialized = 1;
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_shutdown(vflash_renderer_t *r)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    vflash_gpu_wait_idle(&r->gpu);
    vflash_gpu_result_t res = vflash_gpu_shutdown(&r->gpu);
    r->initialized = 0;
    return res;
}

vflash_gpu_result_t vflash_renderer_reset_atlas(vflash_renderer_t *r)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    vflash_gpu_result_t res = vflash_gpu_wait_idle(&r->gpu);
    if (res) return res;
    if (!vflash_video_restore_linear()) return VFLASH_GPU_DEVICE_ERROR;
    r->alloc_x = r->config.atlas_x;
    r->alloc_y = r->config.atlas_y;
    r->alloc_row_h = 0;
    r->generation_id++;
    if (!r->generation_id) r->generation_id = 1;
    r->current = &r->surf_a;
    vflash_gpu_clip(r->current, r->clip_x, r->clip_y, r->clip_w, r->clip_h);
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_destroy_texture(vflash_renderer_t *r, vflash_texture_t *tex)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !tex->id || tex->id != r->generation_id) return VFLASH_GPU_INVALID;
    tex->id = 0;
    tex->width = 0;
    tex->height = 0;
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_upload_texture(vflash_renderer_t *r, vflash_texture_t *tex,
                                                   const void *pixels, uint32_t width, uint32_t height,
                                                   vflash_texture_type_t type)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !pixels || !width || !height) return VFLASH_GPU_INVALID;
    if (width & 1u) return VFLASH_GPU_INVALID;
    if (type > VFLASH_TEXTURE_TRANSLUCENT) return VFLASH_GPU_INVALID;
    if (width > r->config.atlas_width || height > r->config.atlas_height) return VFLASH_GPU_INVALID;

    uint32_t front = vflash_video_tiled_address();
    if (front) {
        uint32_t fbytes = vflash_video_tiled_bytes();
        uint32_t sa_bytes = (r->surf_a.fb.height >> 3) * 16384u;
        uint32_t sb_bytes = (r->surf_b.fb.height >> 3) * 16384u;
        if ((r->surf_a.physical_address < front + fbytes && front < r->surf_a.physical_address + sa_bytes) ||
            (r->surf_b.physical_address < front + fbytes && front < r->surf_b.physical_address + sb_bytes)) {
            return VFLASH_GPU_BUSY;
        }
    }

    uint32_t alloc_w = (width + 3u) & ~3u;
    uint32_t next_x = r->alloc_x;
    uint32_t next_y = r->alloc_y;
    uint32_t next_row_h = r->alloc_row_h;

    if (next_x + alloc_w > r->config.atlas_x + r->config.atlas_width) {
        next_x = r->config.atlas_x;
        next_y += next_row_h;
        next_row_h = 0;
    }
    if (next_y + height > r->config.atlas_y + r->config.atlas_height) {
        return VFLASH_GPU_OUT_OF_MEMORY;
    }

    uint32_t ax = next_x;
    uint32_t ay = next_y;
    next_x += alloc_w;
    if (height > next_row_h) next_row_h = height;

    vflash_framebuffer_t src_fb;
    src_fb.pixels = (uint16_t *)(uintptr_t)pixels;
    src_fb.width = width;
    src_fb.height = height;
    src_fb.stride = width;
    src_fb.capacity_pixels = width * height;
    src_fb.pixel_format = VFLASH_BGR555;

    vflash_gpu_surface_t src_surf;
    vflash_gpu_result_t res = vflash_gpu_surface_register(&src_surf, &src_fb, (uint32_t)(uintptr_t)pixels, 1);
    if (res) return res;

    // Temporary full clip required by vflash_gpu_upload
    vflash_gpu_clip(&r->surf_a, 0, 0, (int32_t)r->config.surface_width, (int32_t)r->config.surface_height);
    vflash_gpu_clip(&r->surf_b, 0, 0, (int32_t)r->config.surface_width, (int32_t)r->config.surface_height);

    res = vflash_gpu_upload(&r->gpu, &r->surf_a, &src_surf, 0, 0, (int32_t)width, (int32_t)height, (int32_t)ax, (int32_t)ay);
    if (!res) {
        res = vflash_gpu_upload(&r->gpu, &r->surf_b, &src_surf, 0, 0, (int32_t)width, (int32_t)height, (int32_t)ax, (int32_t)ay);
    }

    // Restore active clip settings on both surfaces
    vflash_gpu_clip(&r->surf_a, r->clip_x, r->clip_y, r->clip_w, r->clip_h);
    vflash_gpu_clip(&r->surf_b, r->clip_x, r->clip_y, r->clip_w, r->clip_h);
    if (res) return res;

    r->alloc_x = next_x;
    r->alloc_y = next_y;
    r->alloc_row_h = next_row_h;

    tex->atlas_x = ax;
    tex->atlas_y = ay;
    tex->width = width;
    tex->height = height;
    tex->type = type;
    tex->id = r->generation_id;
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_clear(vflash_renderer_t *r, uint32_t rgb)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    return vflash_gpu_fill_rect(&r->gpu, r->current, 0, 0, (int32_t)r->config.playfield_width, (int32_t)r->config.playfield_height, rgb);
}

vflash_gpu_result_t vflash_renderer_fill_rect(vflash_renderer_t *r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    return vflash_gpu_fill_rect(&r->gpu, r->current, x, y, w, h, rgb);
}

vflash_gpu_result_t vflash_renderer_set_clip(vflash_renderer_t *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    int32_t pw = (int32_t)r->config.playfield_width;
    int32_t ph = (int32_t)r->config.playfield_height;
    int32_t l = x < 0 ? 0 : x;
    int32_t t = y < 0 ? 0 : y;
    int32_t rgt = ((int64_t)x + w > pw) ? pw : (int32_t)(x + w);
    int32_t bot = ((int64_t)y + h > ph) ? ph : (int32_t)(y + h);
    int32_t cw = rgt > l ? rgt - l : 0;
    int32_t ch = bot > t ? bot - t : 0;
    r->clip_x = l;
    r->clip_y = t;
    r->clip_w = cw;
    r->clip_h = ch;
    return vflash_gpu_clip(r->current, l, t, cw, ch);
}

vflash_gpu_result_t vflash_renderer_reset_clip(vflash_renderer_t *r)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    r->clip_x = 0;
    r->clip_y = 0;
    r->clip_w = (int32_t)r->config.playfield_width;
    r->clip_h = (int32_t)r->config.playfield_height;
    return vflash_gpu_clip(r->current, 0, 0, r->clip_w, r->clip_h);
}

vflash_gpu_result_t vflash_renderer_draw_sub(vflash_renderer_t *r, const vflash_texture_t *tex,
                                             int32_t sx, int32_t sy, int32_t w, int32_t h,
                                             int32_t dx, int32_t dy)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !tex->id || tex->id != r->generation_id) return VFLASH_GPU_INVALID;
    if (sx < 0 || sy < 0 || w < 0 || h < 0) return VFLASH_GPU_INVALID;
    if ((uint32_t)sx + (uint32_t)w > tex->width || (uint32_t)sy + (uint32_t)h > tex->height) return VFLASH_GPU_INVALID;
    int32_t ax = (int32_t)tex->atlas_x + sx;
    int32_t ay = (int32_t)tex->atlas_y + sy;

    if (tex->type == VFLASH_TEXTURE_OPAQUE) {
        int32_t l = dx;
        int32_t t = dy;
        int32_t r_edge = dx + w;
        int32_t b_edge = dy + h;
        if (l < r->clip_x) l = r->clip_x;
        if (t < r->clip_y) t = r->clip_y;
        if (r_edge > r->clip_x + r->clip_w) r_edge = r->clip_x + r->clip_w;
        if (b_edge > r->clip_y + r->clip_h) b_edge = r->clip_y + r->clip_h;
        if (l >= r_edge || t >= b_edge) return VFLASH_GPU_OK;

        int32_t csx = ax + (l - dx);
        int32_t csy = ay + (t - dy);
        int32_t cw = r_edge - l;
        int32_t ch = b_edge - t;

        // Low-level D2 copy requires surface clip to cover the full surface
        vflash_gpu_clip(r->current, 0, 0, (int32_t)r->config.surface_width, (int32_t)r->config.surface_height);
        vflash_gpu_result_t res = vflash_gpu_copy_region(&r->gpu, r->current, csx, csy, cw, ch, l, t);
        vflash_gpu_clip(r->current, r->clip_x, r->clip_y, r->clip_w, r->clip_h);
        return res;
    }
    if (tex->type == VFLASH_TEXTURE_KEYED) {
        return vflash_gpu_draw_sprite_ex(&r->gpu, r->current, VFLASH_GPU_TEXTURE_BGR555_KEY, ax, ay, w, h, dx, dy);
    }
    if (tex->type == VFLASH_TEXTURE_TRANSLUCENT) {
        return vflash_gpu_draw_sprite_ex(&r->gpu, r->current, VFLASH_GPU_TEXTURE_ABGR4444, ax, ay, w, h, dx, dy);
    }
    return VFLASH_GPU_INVALID;
}

vflash_gpu_result_t vflash_renderer_draw(vflash_renderer_t *r, const vflash_texture_t *tex, int32_t dx, int32_t dy)
{
    if (!tex) return VFLASH_GPU_INVALID;
    return vflash_renderer_draw_sub(r, tex, 0, 0, (int32_t)tex->width, (int32_t)tex->height, dx, dy);
}

vflash_gpu_result_t vflash_renderer_present(vflash_renderer_t *r)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    vflash_gpu_result_t res = vflash_gpu_wait_idle(&r->gpu);
    if (res) return res;
    res = vflash_gpu_present_view(&r->gpu, r->current, r->config.playfield_width, r->config.playfield_height,
                                  r->config.screen_x, r->config.screen_y);
    if (res) return res;
    r->frame_count++;
    r->current = (r->current == &r->surf_a) ? &r->surf_b : &r->surf_a;
    vflash_gpu_clip(r->current, r->clip_x, r->clip_y, r->clip_w, r->clip_h);
    return VFLASH_GPU_OK;
}
