#include "vflash/renderer.h"
#include "vflash/clock.h"
#include "vflash/draw.h"
#include "vflash/mmu.h"
#include <string.h>

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
    if (cfg->atlas_width > 0 && cfg->atlas_height > 0) {
        r->atlas_free_count = 1;
        r->atlas_free[0].x = (uint16_t)cfg->atlas_x;
        r->atlas_free[0].y = (uint16_t)cfg->atlas_y;
        r->atlas_free[0].w = (uint16_t)cfg->atlas_width;
        r->atlas_free[0].h = (uint16_t)cfg->atlas_height;
    } else {
        r->atlas_free_count = 0;
    }
    r->atlas_live_count = 0;
    r->atlas_alloc_fail_oom = 0;
    r->atlas_alloc_fail_frag = 0;
    r->atlas_alloc_fail_meta = 0;
    r->clip_x = 0;
    r->clip_y = 0;
    r->clip_w = (int32_t)cfg->playfield_width;
    r->clip_h = (int32_t)cfg->playfield_height;
    vflash_gpu_clip(&r->surf_a, 0, 0, r->clip_w, r->clip_h);
    vflash_gpu_clip(&r->surf_b, 0, 0, r->clip_w, r->clip_h);
    r->generation_id = 1;
    r->has_target = 0;
    r->target_op_count = 0;
    r->target_op_overflow = 0;
    r->atlas_sync_pending = 0;
    r->dirty_tiles_all = 0;
    memset(r->dirty_tiles, 0, sizeof(r->dirty_tiles));
    r->perf_present_calls = 0;
    r->perf_wait_idle_ticks = 0;
    r->perf_present_view_ticks = 0;
    r->perf_sync_count = 0;
    r->perf_sync_tiles = 0;
    r->perf_sync_bytes = 0;
    r->perf_sync_ticks = 0;
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

static void init_atlas_free(vflash_renderer_t *r)
{
    if (r->config.atlas_width > 0 && r->config.atlas_height > 0) {
        r->atlas_free_count = 1;
        r->atlas_free[0].x = (uint16_t)r->config.atlas_x;
        r->atlas_free[0].y = (uint16_t)r->config.atlas_y;
        r->atlas_free[0].w = (uint16_t)r->config.atlas_width;
        r->atlas_free[0].h = (uint16_t)r->config.atlas_height;
    } else {
        r->atlas_free_count = 0;
    }
}

vflash_gpu_result_t vflash_renderer_reset_atlas(vflash_renderer_t *r)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    vflash_gpu_result_t res = vflash_gpu_wait_idle(&r->gpu);
    if (res) return res;
    if (!vflash_video_restore_linear()) return VFLASH_GPU_DEVICE_ERROR;
    init_atlas_free(r);
    r->atlas_live_count = 0;
    r->atlas_alloc_fail_oom = 0;
    r->atlas_alloc_fail_frag = 0;
    r->atlas_alloc_fail_meta = 0;
    r->generation_id++;
    if (!r->generation_id) r->generation_id = 1;
    r->current = &r->surf_a;
    r->has_target = 0;
    r->target_op_count = 0;
    r->target_op_overflow = 0;
    r->atlas_sync_pending = 0;
    r->dirty_tiles_all = 0;
    memset(r->dirty_tiles, 0, sizeof(r->dirty_tiles));
    r->clip_x = 0;
    r->clip_y = 0;
    r->clip_w = (int32_t)r->config.playfield_width;
    r->clip_h = (int32_t)r->config.playfield_height;
    vflash_gpu_clip(r->current, r->clip_x, r->clip_y, r->clip_w, r->clip_h);
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_get_atlas_stats(const vflash_renderer_t *r,
                                                    vflash_atlas_stats_t *out)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!out) return VFLASH_GPU_INVALID;

    out->total_texels = r->config.atlas_width * r->config.atlas_height;
    out->free_texels = 0;
    out->largest_free_texels = 0;
    out->largest_free_w = 0;
    out->largest_free_h = 0;
    out->free_rect_count = r->atlas_free_count;
    out->live_count = r->atlas_live_count;
    out->fail_oom = r->atlas_alloc_fail_oom;
    out->fail_frag = r->atlas_alloc_fail_frag;
    out->fail_meta = r->atlas_alloc_fail_meta;

    for (uint32_t i = 0; i < r->atlas_free_count; ++i) {
        uint32_t w = r->atlas_free[i].w;
        uint32_t h = r->atlas_free[i].h;
        uint32_t area = w * h;
        out->free_texels += area;
        if (area > out->largest_free_texels) {
            out->largest_free_texels = area;
            out->largest_free_w = w;
            out->largest_free_h = h;
        }
    }
    return VFLASH_GPU_OK;
}

static void free_atlas_slot(vflash_renderer_t *r, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h)
{
    if (!w || !h) return;
    if (r->atlas_live_count > 0) {
        r->atlas_live_count--;
    }

    // Invariant: atlas_free_count + atlas_live_count <= VFLASH_MAX_ATLAS_FREE_RECTS
    // at all times, so decrementing live_count guarantees room for +1 free rect.
    vflash_atlas_rect_t *rects = r->atlas_free;
    uint32_t n = r->atlas_free_count;
    if (n < VFLASH_MAX_ATLAS_FREE_RECTS) {
        rects[n].x = (uint16_t)x;
        rects[n].y = (uint16_t)y;
        rects[n].w = (uint16_t)w;
        rects[n].h = (uint16_t)h;
        n++;
    } else {
        for (uint32_t i = 0; i < n; ++i) {
            vflash_atlas_rect_t *a = &rects[i];
            if (a->x == x && a->w == w &&
                (a->y + a->h == y || y + h == a->y)) {
                if (y < a->y) a->y = (uint16_t)y;
                a->h = (uint16_t)(a->h + h);
                break;
            }
            if (a->y == y && a->h == h &&
                (a->x + a->w == x || x + w == a->x)) {
                if (x < a->x) a->x = (uint16_t)x;
                a->w = (uint16_t)(a->w + w);
                break;
            }
        }
    }

    int merged = 1;
    while (merged) {
        merged = 0;
        for (uint32_t i = 0; i < n && !merged; ++i) {
            for (uint32_t j = i + 1; j < n; ++j) {
                vflash_atlas_rect_t *a = &rects[i];
                vflash_atlas_rect_t *b = &rects[j];
                if (a->x == b->x && a->w == b->w &&
                    (a->y + a->h == b->y || b->y + b->h == a->y)) {
                    if (b->y < a->y) a->y = b->y;
                    a->h = (uint16_t)(a->h + b->h);
                    rects[j] = rects[--n];
                    merged = 1;
                    break;
                }
                if (a->y == b->y && a->h == b->h &&
                    (a->x + a->w == b->x || b->x + b->w == a->x)) {
                    if (b->x < a->x) a->x = b->x;
                    a->w = (uint16_t)(a->w + b->w);
                    rects[j] = rects[--n];
                    merged = 1;
                    break;
                }
            }
        }
    }
    r->atlas_free_count = n;

    uint32_t free_area = 0;
    for (uint32_t i = 0; i < n; ++i) {
        free_area += (uint32_t)rects[i].w * (uint32_t)rects[i].h;
    }
    if (free_area == r->config.atlas_width * r->config.atlas_height) {
        init_atlas_free(r);
    }
}

static vflash_gpu_result_t alloc_atlas_slot(vflash_renderer_t *r,
                                            uint32_t width, uint32_t height,
                                            uint32_t *ax_out, uint32_t *ay_out)
{
    uint32_t alloc_w = (width + 3u) & ~3u;
    uint32_t best_idx = VFLASH_MAX_ATLAS_FREE_RECTS;
    uint32_t best_short = 0xFFFFFFFFu;
    uint32_t best_area = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < r->atlas_free_count; ++i) {
        uint32_t rw = r->atlas_free[i].w;
        uint32_t rh = r->atlas_free[i].h;
        if (rw < alloc_w || rh < height) continue;
        uint32_t rem_w = rw - alloc_w;
        uint32_t rem_h = rh - height;
        uint32_t short_side = (rem_w < rem_h) ? rem_w : rem_h;
        uint32_t area = rw * rh;
        if (short_side < best_short ||
            (short_side == best_short && area < best_area)) {
            best_short = short_side;
            best_area = area;
            best_idx = i;
        }
    }

    if (best_idx >= r->atlas_free_count) {
        uint32_t free_area = 0;
        for (uint32_t i = 0; i < r->atlas_free_count; ++i) {
            free_area += (uint32_t)r->atlas_free[i].w *
                         (uint32_t)r->atlas_free[i].h;
        }
        if (free_area >= alloc_w * height) {
            r->atlas_alloc_fail_frag++;
        } else {
            r->atlas_alloc_fail_oom++;
        }
        return VFLASH_GPU_OUT_OF_MEMORY;
    }

    vflash_atlas_rect_t chosen = r->atlas_free[best_idx];
    uint32_t rem_w = chosen.w - alloc_w;
    uint32_t rem_h = chosen.h - height;
    uint32_t needed_slots = (rem_w > 0 ? 1u : 0u) + (rem_h > 0 ? 1u : 0u);
    // Reserve 1 metadata slot for this live texture so freeing it later
    // can never overflow VFLASH_MAX_ATLAS_FREE_RECTS or lose atlas space.
    if (r->atlas_free_count + r->atlas_live_count + needed_slots >
        VFLASH_MAX_ATLAS_FREE_RECTS) {
        r->atlas_alloc_fail_meta++;
        return VFLASH_GPU_OUT_OF_MEMORY;
    }

    r->atlas_free[best_idx] = r->atlas_free[--r->atlas_free_count];
    r->atlas_live_count++;
    vflash_atlas_rect_t right = { 0, 0, 0, 0 };
    vflash_atlas_rect_t bottom = { 0, 0, 0, 0 };
    if (rem_w > rem_h) {
        // Split vertically: right gets full height, bottom gets alloc_w.
        if (rem_w > 0) {
            right.x = (uint16_t)(chosen.x + alloc_w);
            right.y = chosen.y;
            right.w = (uint16_t)rem_w;
            right.h = chosen.h;
        }
        if (rem_h > 0) {
            bottom.x = chosen.x;
            bottom.y = (uint16_t)(chosen.y + height);
            bottom.w = (uint16_t)alloc_w;
            bottom.h = (uint16_t)rem_h;
        }
    } else {
        // Split horizontally: right gets alloc height, bottom gets full width.
        if (rem_w > 0) {
            right.x = (uint16_t)(chosen.x + alloc_w);
            right.y = chosen.y;
            right.w = (uint16_t)rem_w;
            right.h = (uint16_t)height;
        }
        if (rem_h > 0) {
            bottom.x = chosen.x;
            bottom.y = (uint16_t)(chosen.y + height);
            bottom.w = chosen.w;
            bottom.h = (uint16_t)rem_h;
        }
    }

    if (right.w > 0 && right.h > 0 &&
        r->atlas_free_count < VFLASH_MAX_ATLAS_FREE_RECTS) {
        r->atlas_free[r->atlas_free_count++] = right;
    }
    if (bottom.w > 0 && bottom.h > 0 &&
        r->atlas_free_count < VFLASH_MAX_ATLAS_FREE_RECTS) {
        r->atlas_free[r->atlas_free_count++] = bottom;
    }

    *ax_out = chosen.x;
    *ay_out = chosen.y;
    return VFLASH_GPU_OK;
}

static void active_origin_bounds(const vflash_renderer_t *r,
                                 int32_t *ox, int32_t *oy,
                                 int32_t *bw, int32_t *bh)
{
    if (r->has_target) {
        *ox = (int32_t)r->target.atlas_x;
        *oy = (int32_t)r->target.atlas_y;
        *bw = (int32_t)r->target.width;
        *bh = (int32_t)r->target.height;
    } else {
        *ox = 0;
        *oy = 0;
        *bw = (int32_t)r->config.playfield_width;
        *bh = (int32_t)r->config.playfield_height;
    }
}

static vflash_gpu_surface_t *other_surface(vflash_renderer_t *r)
{
    return (r->current == &r->surf_a) ? &r->surf_b : &r->surf_a;
}

static int can_touch_surface(const vflash_gpu_surface_t *s)
{
    uint32_t front = vflash_video_tiled_address();
    if (!front) return 1;
    uint32_t fbytes = vflash_video_tiled_bytes();
    uint32_t sbytes = (s->fb.height >> 3) * 16384u;
    return !(s->physical_address < front + fbytes &&
             front < s->physical_address + sbytes);
}

static vflash_gpu_result_t exec_target_op(vflash_renderer_t *r,
                                          vflash_gpu_surface_t *surf,
                                          const vflash_target_op_t *op)
{
    if (op->op == 0) {
        vflash_gpu_clip(surf, op->cx, op->cy, op->cw, op->ch);
        return vflash_gpu_fill_rect(&r->gpu, surf,
                                    op->dx, op->dy, op->w, op->h, op->rgb);
    }
    if (op->op == 1) {
        vflash_gpu_clip(surf, 0, 0,
                        (int32_t)r->config.surface_width,
                        (int32_t)r->config.surface_height);
        return vflash_gpu_copy_region(&r->gpu, surf,
                                      op->sx, op->sy, op->w, op->h,
                                      op->dx, op->dy);
    }
    vflash_gpu_clip(surf, op->cx, op->cy, op->cw, op->ch);
    vflash_gpu_texture_format_t fmt = (op->op == 3)
        ? VFLASH_GPU_TEXTURE_ABGR4444
        : VFLASH_GPU_TEXTURE_BGR555_KEY;
    return vflash_gpu_draw_sprite_ex(&r->gpu, surf, fmt,
                                     op->sx, op->sy, op->w, op->h,
                                     op->dx, op->dy);
}

static void mark_atlas_dirty_rect(vflash_renderer_t *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0) return;
    if (!r->config.atlas_width || !r->config.atlas_height) return;
    int32_t ax0 = (int32_t)r->config.atlas_x;
    int32_t ay0 = (int32_t)r->config.atlas_y;
    int32_t ax1 = ax0 + (int32_t)r->config.atlas_width;
    int32_t ay1 = ay0 + (int32_t)r->config.atlas_height;
    int32_t x0 = x < ax0 ? ax0 : x;
    int32_t y0 = y < ay0 ? ay0 : y;
    int32_t x1 = (x + w) > ax1 ? ax1 : (x + w);
    int32_t y1 = (y + h) > ay1 ? ay1 : (y + h);
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t tx0 = (uint32_t)x0 >> 5;
    uint32_t tx1 = (uint32_t)(x1 - 1) >> 5;
    uint32_t ty0 = (uint32_t)y0 >> 3;
    uint32_t ty1 = (uint32_t)(y1 - 1) >> 3;
    if (tx1 >= 32u || ty1 >= 32u) {
        r->dirty_tiles_all = 1;
        return;
    }

    uint32_t hi = (tx1 == 31u) ? 0xFFFFFFFFu : ((1u << (tx1 + 1u)) - 1u);
    uint32_t lo = ~((1u << tx0) - 1u);
    uint32_t mask = hi & lo;
    for (uint32_t ty = ty0; ty <= ty1; ++ty) {
        r->dirty_tiles[ty] |= mask;
    }
}

static void sync_atlas_tiles(vflash_renderer_t *r,
                             vflash_gpu_surface_t *src,
                             vflash_gpu_surface_t *dst)
{
    if (!r->config.atlas_width || !r->config.atlas_height) return;
    uint16_t t0 = vflash_clock_ticks();
    uint32_t tx0 = r->config.atlas_x >> 5;
    uint32_t tx1 = (r->config.atlas_x + r->config.atlas_width - 1u) >> 5;
    uint32_t ty0 = r->config.atlas_y >> 3;
    uint32_t ty1 = (r->config.atlas_y + r->config.atlas_height - 1u) >> 3;
    const uint8_t *sbase = (const uint8_t *)src->fb.pixels;
    uint8_t *dbase = (uint8_t *)dst->fb.pixels;
    uint32_t tiles = 0;

    for (uint32_t ty = ty0; ty <= ty1; ++ty) {
        uint32_t row_mask = (!r->dirty_tiles_all && ty < 32u) ? r->dirty_tiles[ty] : 0xFFFFFFFFu;
        if (!row_mask) continue;
        uint32_t row_off = ty * 16384u;
        for (uint32_t tx = tx0; tx <= tx1; ++tx) {
            if (!r->dirty_tiles_all && tx < 32u && !(row_mask & (1u << tx))) {
                continue;
            }
            uint32_t off = row_off + tx * 512u;
            vflash_dcache_invalidate_range((void *)(uintptr_t)(sbase + off), 512u);
            memcpy(dbase + off, sbase + off, 512u);
            vflash_dcache_clean_range(dbase + off, 512u);
            tiles++;
        }
    }
    uint16_t t1 = vflash_clock_ticks();
    r->perf_sync_count++;
    r->perf_sync_tiles += tiles;
    r->perf_sync_bytes += tiles * 512u;
    r->perf_sync_ticks += (uint16_t)(t1 - t0);
}

static vflash_gpu_result_t submit_target_op(vflash_renderer_t *r,
                                            const vflash_target_op_t *op)
{
    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    vflash_gpu_result_t res = exec_target_op(r, r->current, op);
    vflash_gpu_clip(r->current, ox + r->clip_x, oy + r->clip_y,
                    r->clip_w, r->clip_h);
    if (res) return res;

    vflash_gpu_surface_t *other = other_surface(r);
    if (can_touch_surface(other)) {
        res = exec_target_op(r, other, op);
        vflash_gpu_clip(other, ox + r->clip_x, oy + r->clip_y,
                        r->clip_w, r->clip_h);
        return res;
    }

    if (r->target_op_overflow) {
        mark_atlas_dirty_rect(r, op->cx, op->cy, op->cw, op->ch);
        return VFLASH_GPU_OK;
    }
    mark_atlas_dirty_rect(r, op->cx, op->cy, op->cw, op->ch);
    if (r->target_op_count >= VFLASH_MAX_TARGET_OPS) {
        r->target_op_overflow = 1;
        return VFLASH_GPU_OK;
    }
    r->target_ops[r->target_op_count++] = *op;
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_destroy_texture(vflash_renderer_t *r, vflash_texture_t *tex)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !tex->id || tex->id != r->generation_id) return VFLASH_GPU_INVALID;
    uint32_t alloc_w = (tex->width + 3u) & ~3u;
    if (tex->width > 0 && tex->height > 0 &&
        tex->atlas_x >= r->config.atlas_x &&
        tex->atlas_y >= r->config.atlas_y &&
        tex->atlas_x + alloc_w <= r->config.atlas_x + r->config.atlas_width &&
        tex->atlas_y + tex->height <= r->config.atlas_y + r->config.atlas_height) {
        free_atlas_slot(r, tex->atlas_x, tex->atlas_y, alloc_w, tex->height);
    }
    tex->id = 0;
    tex->width = 0;
    tex->height = 0;
    return VFLASH_GPU_OK;
}

static vflash_gpu_result_t upload_to_surfaces(
    vflash_renderer_t *r, const vflash_gpu_surface_t *src_surf,
    uint32_t width, uint32_t height, int32_t ax, int32_t ay)
{
    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    (void)bw;
    (void)bh;

    vflash_gpu_clip(r->current, 0, 0,
                    (int32_t)r->config.surface_width,
                    (int32_t)r->config.surface_height);
    vflash_gpu_result_t res = vflash_gpu_upload(
        &r->gpu, r->current, src_surf, 0, 0,
        (int32_t)width, (int32_t)height, ax, ay);
    vflash_gpu_clip(r->current, ox + r->clip_x, oy + r->clip_y,
                    r->clip_w, r->clip_h);
    if (res) return res;

    vflash_gpu_surface_t *other = other_surface(r);
    if (can_touch_surface(other)) {
        vflash_gpu_clip(other, 0, 0,
                        (int32_t)r->config.surface_width,
                        (int32_t)r->config.surface_height);
        res = vflash_gpu_upload(
            &r->gpu, other, src_surf, 0, 0,
            (int32_t)width, (int32_t)height, ax, ay);
        vflash_gpu_clip(other, ox + r->clip_x, oy + r->clip_y,
                        r->clip_w, r->clip_h);
        return res;
    }

    r->atlas_sync_pending = 1;
    mark_atlas_dirty_rect(r, ax, ay, (int32_t)width, (int32_t)height);
    return VFLASH_GPU_OK;
}

static vflash_gpu_result_t upload_texture_impl(vflash_renderer_t *r, vflash_texture_t *tex,
                                               const void *pixels, uint32_t width, uint32_t height,
                                               vflash_texture_type_t type, int allow_live)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !pixels || !width || !height) return VFLASH_GPU_INVALID;
    if (width & 1u) return VFLASH_GPU_INVALID;
    if (type > VFLASH_TEXTURE_TRANSLUCENT) return VFLASH_GPU_INVALID;
    if (width > r->config.atlas_width || height > r->config.atlas_height) return VFLASH_GPU_INVALID;

    if (allow_live) {
        if (!can_touch_surface(r->current)) {
            return VFLASH_GPU_BUSY;
        }
    } else if (!can_touch_surface(&r->surf_a) || !can_touch_surface(&r->surf_b)) {
        return VFLASH_GPU_BUSY;
    }

    uint32_t ax = 0, ay = 0;
    vflash_gpu_result_t res = alloc_atlas_slot(r, width, height, &ax, &ay);
    if (res) return res;

    vflash_framebuffer_t src_fb;
    src_fb.pixels = (uint16_t *)(uintptr_t)pixels;
    src_fb.width = width;
    src_fb.height = height;
    src_fb.stride = width;
    src_fb.capacity_pixels = width * height;
    src_fb.pixel_format = VFLASH_BGR555;

    vflash_gpu_surface_t src_surf;
    res = vflash_gpu_surface_register(&src_surf, &src_fb, (uint32_t)(uintptr_t)pixels, 1);
    if (res) {
        free_atlas_slot(r, ax, ay, (width + 3u) & ~3u, height);
        return res;
    }

    res = upload_to_surfaces(r, &src_surf, width, height, (int32_t)ax, (int32_t)ay);
    if (res) {
        free_atlas_slot(r, ax, ay, (width + 3u) & ~3u, height);
        return res;
    }

    tex->atlas_x = ax;
    tex->atlas_y = ay;
    tex->width = width;
    tex->height = height;
    tex->type = type;
    tex->id = r->generation_id;
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_upload_texture(vflash_renderer_t *r, vflash_texture_t *tex,
                                                   const void *pixels, uint32_t width, uint32_t height,
                                                   vflash_texture_type_t type)
{
    return upload_texture_impl(r, tex, pixels, width, height, type, 0);
}

vflash_gpu_result_t vflash_renderer_upload_texture_live(vflash_renderer_t *r, vflash_texture_t *tex,
                                                        const void *pixels, uint32_t width, uint32_t height,
                                                        vflash_texture_type_t type)
{
    return upload_texture_impl(r, tex, pixels, width, height, type, 1);
}

vflash_gpu_result_t vflash_renderer_update_texture(
    vflash_renderer_t *r, vflash_texture_t *tex, const void *pixels,
    uint32_t width, uint32_t height, vflash_texture_type_t type)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !pixels || !width || !height) return VFLASH_GPU_INVALID;
    if (!tex->id || tex->id != r->generation_id) return VFLASH_GPU_INVALID;
    if (tex->width != width || tex->height != height) return VFLASH_GPU_INVALID;
    if (type > VFLASH_TEXTURE_TRANSLUCENT) return VFLASH_GPU_INVALID;

    vflash_framebuffer_t src_fb;
    src_fb.pixels = (uint16_t *)(uintptr_t)pixels;
    src_fb.width = width;
    src_fb.height = height;
    src_fb.stride = width;
    src_fb.capacity_pixels = width * height;
    src_fb.pixel_format = VFLASH_BGR555;

    vflash_gpu_surface_t src_surf;
    vflash_gpu_result_t res = vflash_gpu_surface_register(
        &src_surf, &src_fb, (uint32_t)(uintptr_t)pixels, 1);
    if (res) return res;

    res = upload_to_surfaces(r, &src_surf, width, height,
                             (int32_t)tex->atlas_x, (int32_t)tex->atlas_y);
    if (res) return res;

    tex->type = type;
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_create_target(vflash_renderer_t *r, vflash_texture_t *tex,
                                                 uint32_t width, uint32_t height,
                                                 vflash_texture_type_t type)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !width || !height) return VFLASH_GPU_INVALID;
    if (type > VFLASH_TEXTURE_TRANSLUCENT) return VFLASH_GPU_INVALID;
    if (width > r->config.atlas_width || height > r->config.atlas_height) return VFLASH_GPU_INVALID;

    uint32_t ax = 0, ay = 0;
    vflash_gpu_result_t res = alloc_atlas_slot(r, width, height, &ax, &ay);
    if (res) return res;

    vflash_target_op_t op = {
        .op = 0,
        .dx = (int16_t)ax,
        .dy = (int16_t)ay,
        .w = (int16_t)width,
        .h = (int16_t)height,
        .cx = (int16_t)ax,
        .cy = (int16_t)ay,
        .cw = (int16_t)width,
        .ch = (int16_t)height,
        .rgb = 0
    };
    res = submit_target_op(r, &op);
    if (res) {
        free_atlas_slot(r, ax, ay, (width + 3u) & ~3u, height);
        return res;
    }

    tex->atlas_x = ax;
    tex->atlas_y = ay;
    tex->width = width;
    tex->height = height;
    tex->type = type;
    tex->id = r->generation_id;
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_set_target(vflash_renderer_t *r, const vflash_texture_t *target)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!target) {
        if (r->has_target) {
            r->clip_x = r->saved_clip_x;
            r->clip_y = r->saved_clip_y;
            r->clip_w = r->saved_clip_w;
            r->clip_h = r->saved_clip_h;
            r->has_target = 0;
        }
        return vflash_gpu_clip(r->current, r->clip_x, r->clip_y, r->clip_w, r->clip_h);
    }

    if (!target->id || target->id != r->generation_id) return VFLASH_GPU_INVALID;
    if (!target->width || !target->height) return VFLASH_GPU_INVALID;
    if (target->atlas_x < r->config.atlas_x ||
        target->atlas_y < r->config.atlas_y ||
        target->atlas_x + target->width > r->config.atlas_x + r->config.atlas_width ||
        target->atlas_y + target->height > r->config.atlas_y + r->config.atlas_height) {
        return VFLASH_GPU_INVALID;
    }

    if (!r->has_target) {
        r->saved_clip_x = r->clip_x;
        r->saved_clip_y = r->clip_y;
        r->saved_clip_w = r->clip_w;
        r->saved_clip_h = r->clip_h;
    }
    r->has_target = 1;
    r->target = *target;
    r->clip_x = 0;
    r->clip_y = 0;
    r->clip_w = (int32_t)target->width;
    r->clip_h = (int32_t)target->height;
    return vflash_gpu_clip(r->current, (int32_t)target->atlas_x, (int32_t)target->atlas_y,
                           r->clip_w, r->clip_h);
}

vflash_gpu_result_t vflash_renderer_clear(vflash_renderer_t *r, uint32_t rgb)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    if (!r->has_target) {
        return vflash_gpu_fill_rect(&r->gpu, r->current, 0, 0, bw, bh, rgb);
    }
    vflash_target_op_t op = {
        .op = 0,
        .dx = (int16_t)ox,
        .dy = (int16_t)oy,
        .w = (int16_t)bw,
        .h = (int16_t)bh,
        .cx = (int16_t)ox,
        .cy = (int16_t)oy,
        .cw = (int16_t)bw,
        .ch = (int16_t)bh,
        .rgb = rgb
    };
    return submit_target_op(r, &op);
}

vflash_gpu_result_t vflash_renderer_fill_rect(vflash_renderer_t *r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    if (!r->has_target) {
        return vflash_gpu_fill_rect(&r->gpu, r->current, x, y, w, h, rgb);
    }
    vflash_target_op_t op = {
        .op = 0,
        .dx = (int16_t)(ox + x),
        .dy = (int16_t)(oy + y),
        .w = (int16_t)w,
        .h = (int16_t)h,
        .cx = (int16_t)(ox + r->clip_x),
        .cy = (int16_t)(oy + r->clip_y),
        .cw = (int16_t)r->clip_w,
        .ch = (int16_t)r->clip_h,
        .rgb = rgb
    };
    return submit_target_op(r, &op);
}

vflash_gpu_result_t vflash_renderer_set_clip(vflash_renderer_t *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    int32_t l = x < 0 ? 0 : x;
    int32_t t = y < 0 ? 0 : y;
    int32_t rgt = ((int64_t)x + w > bw) ? bw : (int32_t)(x + w);
    int32_t bot = ((int64_t)y + h > bh) ? bh : (int32_t)(y + h);
    int32_t cw = rgt > l ? rgt - l : 0;
    int32_t ch = bot > t ? bot - t : 0;
    r->clip_x = l;
    r->clip_y = t;
    r->clip_w = cw;
    r->clip_h = ch;
    return vflash_gpu_clip(r->current, ox + l, oy + t, cw, ch);
}

vflash_gpu_result_t vflash_renderer_reset_clip(vflash_renderer_t *r)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    r->clip_x = 0;
    r->clip_y = 0;
    r->clip_w = bw;
    r->clip_h = bh;
    return vflash_gpu_clip(r->current, ox, oy, bw, bh);
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

    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);

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

        if (!r->has_target) {
            // Low-level D2 copy requires surface clip to cover the full surface
            vflash_gpu_clip(r->current, 0, 0, (int32_t)r->config.surface_width, (int32_t)r->config.surface_height);
            vflash_gpu_result_t res = vflash_gpu_copy_region(&r->gpu, r->current, csx, csy, cw, ch, l, t);
            vflash_gpu_clip(r->current, r->clip_x, r->clip_y, r->clip_w, r->clip_h);
            return res;
        }
        vflash_target_op_t op = {
            .op = 1,
            .sx = (int16_t)csx,
            .sy = (int16_t)csy,
            .w = (int16_t)cw,
            .h = (int16_t)ch,
            .dx = (int16_t)(ox + l),
            .dy = (int16_t)(oy + t)
        };
        return submit_target_op(r, &op);
    }
    if (tex->type == VFLASH_TEXTURE_KEYED || tex->type == VFLASH_TEXTURE_TRANSLUCENT) {
        vflash_gpu_texture_format_t fmt = (tex->type == VFLASH_TEXTURE_TRANSLUCENT)
            ? VFLASH_GPU_TEXTURE_ABGR4444
            : VFLASH_GPU_TEXTURE_BGR555_KEY;
        if (!r->has_target) {
            return vflash_gpu_draw_sprite_ex(&r->gpu, r->current, fmt, ax, ay, w, h, dx, dy);
        }
        vflash_target_op_t op = {
            .op = (tex->type == VFLASH_TEXTURE_TRANSLUCENT) ? 3 : 2,
            .sx = (int16_t)ax,
            .sy = (int16_t)ay,
            .w = (int16_t)w,
            .h = (int16_t)h,
            .dx = (int16_t)(ox + dx),
            .dy = (int16_t)(oy + dy),
            .cx = (int16_t)(ox + r->clip_x),
            .cy = (int16_t)(oy + r->clip_y),
            .cw = (int16_t)r->clip_w,
            .ch = (int16_t)r->clip_h
        };
        return submit_target_op(r, &op);
    }
    return VFLASH_GPU_INVALID;
}

vflash_gpu_result_t vflash_renderer_draw(vflash_renderer_t *r, const vflash_texture_t *tex, int32_t dx, int32_t dy)
{
    if (!tex) return VFLASH_GPU_INVALID;
    return vflash_renderer_draw_sub(r, tex, 0, 0, (int32_t)tex->width, (int32_t)tex->height, dx, dy);
}

static const uint32_t sin_lut_q16[91] = {
    0, 1144, 2287, 3430, 4572, 5712, 6850, 7987,
    9121, 10252, 11380, 12505, 13626, 14742, 15855, 16962,
    18064, 19161, 20252, 21336, 22415, 23486, 24550, 25607,
    26656, 27697, 28729, 29753, 30767, 31772, 32768, 33754,
    34729, 35693, 36647, 37590, 38521, 39441, 40348, 41243,
    42126, 42995, 43852, 44695, 45525, 46341, 47143, 47930,
    48703, 49461, 50203, 50931, 51643, 52339, 53020, 53684,
    54332, 54963, 55578, 56175, 56756, 57319, 57865, 58393,
    58903, 59396, 59870, 60326, 60764, 61183, 61584, 61966,
    62328, 62672, 62997, 63303, 63589, 63856, 64104, 64332,
    64540, 64729, 64898, 65048, 65177, 65287, 65376, 65446,
    65496, 65526, 65536
};

static int32_t sin_q16(int32_t deg16)
{
    const int32_t full = 360 << 16;
    int32_t a = deg16 % full;
    if (a < 0) a += full;
    int32_t neg = 0;
    if (a >= (180 << 16)) {
        a -= (180 << 16);
        neg = 1;
    }
    if (a > (90 << 16)) {
        a = (180 << 16) - a;
    }
    int32_t idx = a >> 16;
    if (idx >= 90) return neg ? -65536 : 65536;
    int32_t frac = a & 0xFFFF;
    int32_t v0 = (int32_t)sin_lut_q16[idx];
    int32_t v1 = (int32_t)sin_lut_q16[idx + 1];
    int32_t res = v0 + (((v1 - v0) * frac) >> 16);
    return neg ? -res : res;
}

static void sincos_q16(int32_t deg16, int32_t *out_sin, int32_t *out_cos)
{
    *out_sin = sin_q16(deg16);
    *out_cos = sin_q16(deg16 + (90 << 16));
}

static uint8_t log2_pow2_min8(uint32_t dim)
{
    uint8_t k = 0;
    while ((8u << k) < dim && k < 7u) {
        k++;
    }
    return k;
}

static vflash_gpu_texture_format_t tex_gpu_format(const vflash_texture_t *tex)
{
    return (tex->type == VFLASH_TEXTURE_TRANSLUCENT)
        ? VFLASH_GPU_TEXTURE_ABGR4444
        : VFLASH_GPU_TEXTURE_BGR555_KEY;
}

vflash_gpu_result_t vflash_renderer_draw_sub_transformed(
    vflash_renderer_t *r, const vflash_texture_t *tex,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh,
    const vflash_sprite_transform_t *xform)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !tex->id || tex->id != r->generation_id || !xform) {
        return VFLASH_GPU_INVALID;
    }
    if (sx < 0 || sy < 0 || sw < 0 || sh < 0 ||
        xform->dw < 0 || xform->dh < 0) {
        return VFLASH_GPU_INVALID;
    }
    if ((uint32_t)sx + (uint32_t)sw > tex->width ||
        (uint32_t)sy + (uint32_t)sh > tex->height) {
        return VFLASH_GPU_INVALID;
    }
    if (sw == 0 || sh == 0 || xform->dw == 0 || xform->dh == 0 ||
        xform->alpha == 0u) {
        return VFLASH_GPU_OK;
    }

    // Identity fast path via Zevio 2D engine
    if (xform->angle_deg16 == 0 && xform->flip == VFLASH_FLIP_NONE &&
        xform->dw == sw && xform->dh == sh &&
        xform->alpha == 255u && (xform->color_mod & 0xFFFFFFu) == 0xFFFFFFu) {
        return vflash_renderer_draw_sub(r, tex, sx, sy, sw, sh,
                                        xform->dx, xform->dy);
    }

    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    (void)bw;
    (void)bh;

    uint8_t log2_tw = log2_pow2_min8((uint32_t)sw);
    uint8_t log2_th = log2_pow2_min8((uint32_t)sh);

    // Half-texel inset UVs in Q16.16 to prevent atlas neighbor bleeding
    int32_t u_min = (int32_t)(4096u >> log2_tw);
    int32_t u_max = (sw > 1)
        ? (int32_t)(((uint32_t)(sw << 13) - 4096u) >> log2_tw)
        : u_min;
    int32_t v_min = (int32_t)(4096u >> log2_th);
    int32_t v_max = (sh > 1)
        ? (int32_t)(((uint32_t)(sh << 13) - 4096u) >> log2_th)
        : v_min;

    int32_t u_l = (xform->flip & VFLASH_FLIP_HORIZONTAL) ? u_max : u_min;
    int32_t u_r = (xform->flip & VFLASH_FLIP_HORIZONTAL) ? u_min : u_max;
    int32_t v_t = (xform->flip & VFLASH_FLIP_VERTICAL) ? v_max : v_min;
    int32_t v_b = (xform->flip & VFLASH_FLIP_VERTICAL) ? v_min : v_max;

    int32_t lx0 = -xform->pivot_x;
    int32_t ly0 = -xform->pivot_y;
    int32_t lx1 = xform->dw - xform->pivot_x;
    int32_t ly1 = xform->dh - xform->pivot_y;

    int32_t cx_q16 = (ox + xform->dx + xform->pivot_x) << 16;
    int32_t cy_q16 = (oy + xform->dy + xform->pivot_y) << 16;

    int32_t s_q16 = 0, c_q16 = 65536;
    if (xform->angle_deg16 != 0) {
        sincos_q16(xform->angle_deg16, &s_q16, &c_q16);
    }

    const int32_t lx[4] = { lx0, lx1, lx1, lx0 };
    const int32_t ly[4] = { ly0, ly0, ly1, ly1 };
    const int32_t uv_u[4] = { u_l, u_r, u_r, u_l };
    const int32_t uv_v[4] = { v_t, v_t, v_b, v_b };

    vflash_gpu_quad_vertex_t verts[4];
    for (int i = 0; i < 4; i++) {
        verts[i].x = cx_q16 + lx[i] * c_q16 - ly[i] * s_q16;
        verts[i].y = cy_q16 + lx[i] * s_q16 + ly[i] * c_q16;
        verts[i].u = uv_u[i];
        verts[i].v = uv_v[i];
    }

    int32_t ax = (int32_t)tex->atlas_x + sx;
    int32_t ay = (int32_t)tex->atlas_y + sy;
    vflash_gpu_result_t res = vflash_gpu_draw_quad_tex(
        &r->gpu, r->current, tex_gpu_format(tex), ax, ay, log2_tw, log2_th,
        verts, xform->alpha, xform->color_mod);
    if (!res && r->has_target) {
        r->target_op_overflow = 1;
        mark_atlas_dirty_rect(r, (int32_t)r->target.atlas_x, (int32_t)r->target.atlas_y,
                              (int32_t)r->target.width, (int32_t)r->target.height);
    }
    return res;
}

vflash_gpu_result_t vflash_renderer_draw_transformed(
    vflash_renderer_t *r, const vflash_texture_t *tex,
    const vflash_sprite_transform_t *xform)
{
    if (!tex) return VFLASH_GPU_INVALID;
    return vflash_renderer_draw_sub_transformed(
        r, tex, 0, 0, (int32_t)tex->width, (int32_t)tex->height, xform);
}

vflash_gpu_result_t vflash_renderer_draw_tri_tex(
    vflash_renderer_t *r, const vflash_texture_t *tex,
    const vflash_gpu_quad_vertex_t verts[3],
    uint8_t alpha, uint32_t color_mod)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    if (!tex || !tex->id || tex->id != r->generation_id || !verts) {
        return VFLASH_GPU_INVALID;
    }
    if (alpha == 0u) return VFLASH_GPU_OK;

    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    (void)bw;
    (void)bh;

    uint8_t log2_tw = log2_pow2_min8(tex->width);
    uint8_t log2_th = log2_pow2_min8(tex->height);
    int64_t tw_scale = ((int64_t)tex->width << 16) / (int64_t)(8u << log2_tw);
    int64_t th_scale = ((int64_t)tex->height << 16) / (int64_t)(8u << log2_th);

    vflash_gpu_quad_vertex_t tri[3];
    for (int i = 0; i < 3; i++) {
        tri[i].x = verts[i].x + (ox << 16);
        tri[i].y = verts[i].y + (oy << 16);
        tri[i].u = (int32_t)(((int64_t)verts[i].u * tw_scale) >> 16);
        tri[i].v = (int32_t)(((int64_t)verts[i].v * th_scale) >> 16);
    }

    vflash_gpu_result_t res = vflash_gpu_draw_tri_tex(
        &r->gpu, r->current, tex_gpu_format(tex),
        (int32_t)tex->atlas_x, (int32_t)tex->atlas_y,
        log2_tw, log2_th, tri, alpha, color_mod);
    if (!res && r->has_target) {
        r->target_op_overflow = 1;
        mark_atlas_dirty_rect(r, (int32_t)r->target.atlas_x, (int32_t)r->target.atlas_y,
                              (int32_t)r->target.width, (int32_t)r->target.height);
    }
    return res;
}

vflash_gpu_result_t vflash_renderer_present(vflash_renderer_t *r)
{
    if (!r || !r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    r->perf_present_calls++;
    uint16_t t0 = vflash_clock_ticks();
    vflash_gpu_result_t res = vflash_gpu_wait_idle(&r->gpu);
    uint16_t t1 = vflash_clock_ticks();
    r->perf_wait_idle_ticks += (uint16_t)(t1 - t0);
    if (res) return res;

    t0 = vflash_clock_ticks();
    res = vflash_gpu_present_view(&r->gpu, r->current, r->config.playfield_width, r->config.playfield_height,
                                  r->config.screen_x, r->config.screen_y);
    t1 = vflash_clock_ticks();
    r->perf_present_view_ticks += (uint16_t)(t1 - t0);
    if (res) return res;

    vflash_gpu_surface_t *other = other_surface(r);
    if (r->target_op_overflow || r->atlas_sync_pending) {
        t0 = vflash_clock_ticks();
        vflash_gpu_wait_idle(&r->gpu);
        t1 = vflash_clock_ticks();
        r->perf_wait_idle_ticks += (uint16_t)(t1 - t0);
        sync_atlas_tiles(r, r->current, other);
    } else {
        for (uint32_t i = 0; i < r->target_op_count; ++i) {
            res = exec_target_op(r, other, &r->target_ops[i]);
            if (res) {
                r->target_op_count = 0;
                r->target_op_overflow = 0;
                r->atlas_sync_pending = 0;
                r->dirty_tiles_all = 0;
                memset(r->dirty_tiles, 0, sizeof(r->dirty_tiles));
                return res;
            }
        }
    }
    r->target_op_count = 0;
    r->target_op_overflow = 0;
    r->atlas_sync_pending = 0;
    r->dirty_tiles_all = 0;
    memset(r->dirty_tiles, 0, sizeof(r->dirty_tiles));
    r->frame_count++;
    r->current = other;

    int32_t ox, oy, bw, bh;
    active_origin_bounds(r, &ox, &oy, &bw, &bh);
    vflash_gpu_clip(r->current, ox + r->clip_x, oy + r->clip_y, r->clip_w, r->clip_h);
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_get_perf_stats(vflash_renderer_t *r,
                                                   vflash_renderer_perf_stats_t *out)
{
    if (!r || !out) return VFLASH_GPU_INVALID;
    if (!r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    memset(out, 0, sizeof(*out));
    out->present_calls = r->perf_present_calls;
    out->wait_idle_ticks = r->perf_wait_idle_ticks;
    out->present_view_ticks = r->perf_present_view_ticks;
    out->sync_count = r->perf_sync_count;
    out->sync_tiles = r->perf_sync_tiles;
    out->sync_bytes = r->perf_sync_bytes;
    out->sync_ticks = r->perf_sync_ticks;

    vflash_gpu_stats_t gs;
    if (vflash_gpu_stats(&r->gpu, &gs) == VFLASH_GPU_OK) {
        out->gpu_lists = gs.lists;
        out->gpu_submit_ticks = gs.submission_ticks;
        out->gpu_wait_ticks = gs.completion_ticks;
    }
    return VFLASH_GPU_OK;
}

vflash_gpu_result_t vflash_renderer_reset_perf_stats(vflash_renderer_t *r)
{
    if (!r) return VFLASH_GPU_INVALID;
    if (!r->initialized) return VFLASH_GPU_NOT_INITIALIZED;
    r->perf_present_calls = 0;
    r->perf_wait_idle_ticks = 0;
    r->perf_present_view_ticks = 0;
    r->perf_sync_count = 0;
    r->perf_sync_tiles = 0;
    r->perf_sync_bytes = 0;
    r->perf_sync_ticks = 0;
    return VFLASH_GPU_OK;
}
