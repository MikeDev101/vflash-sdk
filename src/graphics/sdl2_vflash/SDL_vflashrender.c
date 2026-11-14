#include "SDL_internal.h"
#include "SDL_hints.h"
#include "render/SDL_sysrender.h"

#include "vflash/renderer.h"
#include "vflash/video.h"
#include "vflash/draw.h"

// Static backing buffers for full 320x240 double-buffering with offscreen atlas
static uint16_t s_surface_a[245760] __attribute__((aligned(64)));
static uint16_t s_surface_b[245760] __attribute__((aligned(64)));
static uint16_t s_conversion_buf[320 * 240] __attribute__((aligned(64)));

typedef struct {
    vflash_renderer_t native_renderer;
    int initialized;
    SDL_Texture *current_target;
} VFLASH_RenderData;

typedef struct {
    vflash_texture_t native_tex;
    int uploaded;
    int is_target;
    vflash_texture_type_t native_type;
} VFLASH_TextureData;

typedef struct {
    SDL_Rect src;
    SDL_FRect dst;
} VFLASH_CopyCmd;

typedef struct {
    SDL_Rect src;
    SDL_FRect dst;
    double angle;
    SDL_FPoint center;
    SDL_RendererFlip flip;
} VFLASH_CopyExCmd;

static void VFLASH_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
    (void)renderer;
    (void)event;
}

static int VFLASH_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    (void)renderer;
    if (w) *w = 320;
    if (h) *h = 240;
    return 0;
}

static int VFLASH_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    if (!rdata || !rdata->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    if (texture->w <= 0 || texture->h <= 0 ||
        texture->w > 320 || texture->h > 240) {
        SDL_SetError("unsupported texture dimensions");
        return -1;
    }
    if (texture->access != SDL_TEXTUREACCESS_TARGET && (texture->w & 1) != 0) {
        SDL_SetError("unsupported texture dimensions");
        return -1;
    }

    VFLASH_TextureData *tdata = (VFLASH_TextureData *)SDL_calloc(1, sizeof(VFLASH_TextureData));
    if (!tdata) {
        SDL_OutOfMemory();
        return -1;
    }

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        vflash_texture_type_t ttype = (texture->blendMode == SDL_BLENDMODE_NONE)
            ? VFLASH_TEXTURE_OPAQUE
            : VFLASH_TEXTURE_KEYED;
        vflash_gpu_result_t res = vflash_renderer_create_target(&rdata->native_renderer,
                                                                &tdata->native_tex,
                                                                (uint32_t)texture->w,
                                                                (uint32_t)texture->h,
                                                                ttype);
        if (res != VFLASH_GPU_OK) {
            SDL_free(tdata);
            SDL_SetError("failed to allocate offscreen render target");
            return -1;
        }
        tdata->is_target = 1;
        tdata->uploaded = 1;
        tdata->native_type = ttype;
    }

    texture->driverdata = tdata;
    return 0;
}

static int VFLASH_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                                const SDL_Rect *rect, const void *pixels, int pitch)
{
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    if (!rdata || !rdata->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }

    VFLASH_TextureData *tdata = (VFLASH_TextureData *)texture->driverdata;
    if (!tdata) {
        SDL_SetError("invalid texture");
        return -1;
    }

    if (rect != NULL) {
        if (rect->x != 0 || rect->y != 0 || rect->w != texture->w || rect->h != texture->h) {
            SDL_SetError("partial texture update unsupported");
            return -1;
        }
    }

    if (rdata->native_renderer.frame_count > 0 && vflash_video_tiled_address() != 0) {
        SDL_SetError("live texture update rejected during tiled scanout");
        return -1;
    }

    vflash_texture_type_t native_type;
    uint16_t *dst = s_conversion_buf;

    if (texture->blendMode == SDL_BLENDMODE_BLEND ||
        texture->format == SDL_PIXELFORMAT_ARGB4444 ||
        texture->format == SDL_PIXELFORMAT_ABGR4444 ||
        texture->format == SDL_PIXELFORMAT_RGBA4444) {
        native_type = VFLASH_TEXTURE_TRANSLUCENT;
        if (texture->format == SDL_PIXELFORMAT_ARGB8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint16_t a4 = (uint16_t)((p >> 28) & 0xFu);
                    uint16_t r4 = (uint16_t)((p >> 20) & 0xFu);
                    uint16_t g4 = (uint16_t)((p >> 12) & 0xFu);
                    uint16_t b4 = (uint16_t)((p >> 4) & 0xFu);
                    dst[y * texture->w + x] = (a4 << 12) | (b4 << 8) | (g4 << 4) | r4;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_RGBA8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint16_t r4 = (uint16_t)((p >> 28) & 0xFu);
                    uint16_t g4 = (uint16_t)((p >> 20) & 0xFu);
                    uint16_t b4 = (uint16_t)((p >> 12) & 0xFu);
                    uint16_t a4 = (uint16_t)((p >> 4) & 0xFu);
                    dst[y * texture->w + x] = (a4 << 12) | (b4 << 8) | (g4 << 4) | r4;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_ABGR8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint16_t a4 = (uint16_t)((p >> 28) & 0xFu);
                    uint16_t b4 = (uint16_t)((p >> 20) & 0xFu);
                    uint16_t g4 = (uint16_t)((p >> 12) & 0xFu);
                    uint16_t r4 = (uint16_t)((p >> 4) & 0xFu);
                    dst[y * texture->w + x] = (a4 << 12) | (b4 << 8) | (g4 << 4) | r4;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_ARGB4444) {
            for (int y = 0; y < texture->h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint16_t p = row[x];
                    uint16_t a4 = (p >> 12) & 0xFu;
                    uint16_t r4 = (p >> 8) & 0xFu;
                    uint16_t g4 = (p >> 4) & 0xFu;
                    uint16_t b4 = p & 0xFu;
                    dst[y * texture->w + x] = (a4 << 12) | (b4 << 8) | (g4 << 4) | r4;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_ABGR4444) {
            for (int y = 0; y < texture->h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    dst[y * texture->w + x] = row[x];
                }
            }
        } else {
            SDL_SetError("unsupported format for blend mode BLEND");
            return -1;
        }
    } else {
        native_type = (texture->blendMode == SDL_BLENDMODE_NONE) ? VFLASH_TEXTURE_OPAQUE : VFLASH_TEXTURE_KEYED;
        if (texture->format == SDL_PIXELFORMAT_RGB555) {
            for (int y = 0; y < texture->h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint16_t p = row[x];
                    uint16_t r = (p >> 10) & 0x1Fu;
                    uint16_t g = (p >> 5) & 0x1Fu;
                    uint16_t b = p & 0x1Fu;
                    dst[y * texture->w + x] = (b << 10) | (g << 5) | r;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_BGR555) {
            for (int y = 0; y < texture->h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    dst[y * texture->w + x] = row[x];
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_ARGB8888 || texture->format == SDL_PIXELFORMAT_RGBA8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint16_t r5 = (texture->format == SDL_PIXELFORMAT_ARGB8888) ?
                                  (uint16_t)((p >> 19) & 0x1Fu) : (uint16_t)((p >> 27) & 0x1Fu);
                    uint16_t g5 = (texture->format == SDL_PIXELFORMAT_ARGB8888) ?
                                  (uint16_t)((p >> 11) & 0x1Fu) : (uint16_t)((p >> 19) & 0x1Fu);
                    uint16_t b5 = (texture->format == SDL_PIXELFORMAT_ARGB8888) ?
                                  (uint16_t)((p >> 3) & 0x1Fu) : (uint16_t)((p >> 11) & 0x1Fu);
                    dst[y * texture->w + x] = (b5 << 10) | (g5 << 5) | r5;
                }
            }
        } else {
            SDL_SetError("unsupported format for opaque/keyed upload");
            return -1;
        }
    }

    vflash_gpu_result_t res;
    if (tdata->uploaded) {
        res = vflash_renderer_update_texture(
            &rdata->native_renderer, &tdata->native_tex,
            dst, (uint32_t)texture->w, (uint32_t)texture->h, native_type);
    } else {
        res = vflash_renderer_upload_texture(
            &rdata->native_renderer, &tdata->native_tex,
            dst, (uint32_t)texture->w, (uint32_t)texture->h, native_type);
    }
    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to upload texture to atlas (%d)", (int)res);
        return -1;
    }

    tdata->uploaded = 1;
    tdata->native_type = native_type;
    return 0;
}

static int VFLASH_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *rect, void **pixels, int *pitch)
{
    (void)renderer;
    (void)texture;
    (void)rect;
    (void)pixels;
    (void)pitch;
    SDL_SetError("LockTexture not supported on static textures");
    return -1;
}

static void VFLASH_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    (void)renderer;
    (void)texture;
}

static void VFLASH_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    (void)renderer;
    (void)texture;
    (void)scaleMode;
}

static int VFLASH_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    if (!rdata || !rdata->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    if (texture == NULL) {
        vflash_gpu_result_t res = vflash_renderer_set_target(&rdata->native_renderer, NULL);
        if (res != VFLASH_GPU_OK) {
            SDL_SetError("failed to restore default render target");
            return -1;
        }
        rdata->current_target = NULL;
        return 0;
    }
    VFLASH_TextureData *tdata = (VFLASH_TextureData *)texture->driverdata;
    if (!tdata || !tdata->is_target) {
        SDL_SetError("texture is not a render target");
        return -1;
    }
    vflash_gpu_result_t res = vflash_renderer_set_target(&rdata->native_renderer, &tdata->native_tex);
    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to select render target");
        return -1;
    }
    rdata->current_target = texture;
    return 0;
}

static int VFLASH_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return 0;
}

static int VFLASH_QueueSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return 0;
}

static int VFLASH_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                  const SDL_FPoint *points, int count)
{
    SDL_FPoint *verts = (SDL_FPoint *)SDL_AllocateRenderVertices(renderer,
                                                                 (size_t)count * sizeof(SDL_FPoint),
                                                                 4, &cmd->data.draw.first);
    if (!verts) {
        return -1;
    }
    cmd->data.draw.count = (size_t)count;
    for (int i = 0; i < count; ++i) {
        verts[i] = points[i];
    }
    return 0;
}

static int VFLASH_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                 const SDL_FPoint *points, int count)
{
    SDL_FPoint *verts = (SDL_FPoint *)SDL_AllocateRenderVertices(renderer,
                                                                 (size_t)count * sizeof(SDL_FPoint),
                                                                 4, &cmd->data.draw.first);
    if (!verts) {
        return -1;
    }
    cmd->data.draw.count = (size_t)count;
    for (int i = 0; i < count; ++i) {
        verts[i] = points[i];
    }
    return 0;
}

static int VFLASH_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                 const SDL_FRect *rects, int count)
{
    SDL_FRect *verts = (SDL_FRect *)SDL_AllocateRenderVertices(renderer,
                                                               (size_t)count * sizeof(SDL_FRect),
                                                               4, &cmd->data.draw.first);
    if (!verts) {
        return -1;
    }

    cmd->data.draw.count = (size_t)count;
    for (int i = 0; i < count; ++i) {
        verts[i] = rects[i];
    }
    return 0;
}

static int VFLASH_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                            const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
    VFLASH_CopyCmd *copy = (VFLASH_CopyCmd *)SDL_AllocateRenderVertices(renderer,
                                                                        sizeof(VFLASH_CopyCmd),
                                                                        4, &cmd->data.draw.first);
    if (!copy) {
        return -1;
    }

    copy->src = *srcrect;
    copy->dst = *dstrect;
    cmd->data.draw.count = 1;
    cmd->data.draw.texture = texture;
    return 0;
}

static int VFLASH_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                              SDL_Texture *texture, const SDL_Rect *srcquad,
                              const SDL_FRect *dstrect, const double angle,
                              const SDL_FPoint *center,
                              const SDL_RendererFlip flip,
                              float scale_x, float scale_y)
{
    VFLASH_CopyExCmd *copy = (VFLASH_CopyExCmd *)SDL_AllocateRenderVertices(
        renderer, sizeof(VFLASH_CopyExCmd), 4, &cmd->data.draw.first);
    if (!copy) {
        return -1;
    }

    copy->src = *srcquad;
    copy->dst.x = dstrect->x * scale_x;
    copy->dst.y = dstrect->y * scale_y;
    copy->dst.w = dstrect->w * scale_x;
    copy->dst.h = dstrect->h * scale_y;
    copy->angle = angle;
    if (center) {
        copy->center.x = center->x * scale_x;
        copy->center.y = center->y * scale_y;
    } else {
        copy->center.x = copy->dst.w * 0.5f;
        copy->center.y = copy->dst.h * 0.5f;
    }
    copy->flip = flip;
    cmd->data.draw.count = 1;
    cmd->data.draw.texture = texture;
    return 0;
}

typedef struct {
    float x;
    float y;
    float u;
    float v;
    SDL_Color color;
} VFLASH_GeomVert;

static int VFLASH_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                SDL_Texture *texture,
                                const float *xy, int xy_stride,
                                const SDL_Color *color, int color_stride,
                                const float *uv, int uv_stride,
                                int num_vertices, const void *indices,
                                int num_indices, int size_indices,
                                float scale_x, float scale_y)
{
    int count = indices ? num_indices : num_vertices;
    if (count <= 0) {
        cmd->data.draw.count = 0;
        return 0;
    }
    VFLASH_GeomVert *verts = (VFLASH_GeomVert *)SDL_AllocateRenderVertices(
        renderer, (size_t)count * sizeof(VFLASH_GeomVert), 4, &cmd->data.draw.first);
    if (!verts) {
        return -1;
    }
    cmd->data.draw.count = (size_t)count;
    cmd->data.draw.texture = texture;
    for (int i = 0; i < count; ++i) {
        int vi = i;
        if (indices) {
            if (size_indices == 1) vi = ((const Uint8 *)indices)[i];
            else if (size_indices == 2) vi = ((const Uint16 *)indices)[i];
            else vi = (int)((const Uint32 *)indices)[i];
        }
        const float *xy_p = (const float *)((const uint8_t *)xy + vi * xy_stride);
        const SDL_Color *c_p = (const SDL_Color *)((const uint8_t *)color + vi * color_stride);
        verts[i].x = xy_p[0] * scale_x;
        verts[i].y = xy_p[1] * scale_y;
        if (texture && uv) {
            const float *uv_p = (const float *)((const uint8_t *)uv + vi * uv_stride);
            verts[i].u = uv_p[0];
            verts[i].v = uv_p[1];
        } else {
            verts[i].u = 0.0f;
            verts[i].v = 0.0f;
        }
        verts[i].color = *c_p;
    }
    return 0;
}

static void draw_solid_triangle(vflash_renderer_t *nr,
                                const VFLASH_GeomVert *a,
                                const VFLASH_GeomVert *b,
                                const VFLASH_GeomVert *c,
                                uint32_t rgb)
{
    if (a->y > b->y) { const VFLASH_GeomVert *t = a; a = b; b = t; }
    if (a->y > c->y) { const VFLASH_GeomVert *t = a; a = c; c = t; }
    if (b->y > c->y) { const VFLASH_GeomVert *t = b; b = c; c = t; }

    int y0 = (int)(a->y + 0.5f);
    int y1 = (int)(b->y + 0.5f);
    int y2 = (int)(c->y + 0.5f);
    if (y0 == y2) return;

    float dy02 = c->y - a->y;
    float dy01 = b->y - a->y;
    float dy12 = c->y - b->y;

    for (int y = y0; y <= y2; ++y) {
        float fy = (float)y + 0.5f;
        if (fy < a->y || fy > c->y) continue;
        float x_long = a->x + (c->x - a->x) * ((fy - a->y) / dy02);
        float x_short;
        if (fy <= b->y) {
            x_short = (dy01 > 0.001f) ? (a->x + (b->x - a->x) * ((fy - a->y) / dy01)) : a->x;
        } else {
            x_short = (dy12 > 0.001f) ? (b->x + (c->x - b->x) * ((fy - b->y) / dy12)) : b->x;
        }
        int xl = (int)((x_long < x_short ? x_long : x_short) + 0.5f);
        int xr = (int)((x_long > x_short ? x_long : x_short) + 0.5f);
        if (xr >= xl) {
            vflash_renderer_fill_rect(nr, xl, y, xr - xl + 1, 1, rgb);
        }
    }
}

static int VFLASH_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                  void *vertices, size_t vertsize)
{
    (void)vertsize;
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    if (!rdata || !rdata->initialized) {
        return -1;
    }
    vflash_renderer_t *nr = &rdata->native_renderer;

    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
            vflash_renderer_set_clip(nr, cmd->data.viewport.rect.x, cmd->data.viewport.rect.y,
                                     cmd->data.viewport.rect.w, cmd->data.viewport.rect.h);
            break;

        case SDL_RENDERCMD_SETCLIPRECT:
            if (cmd->data.cliprect.enabled) {
                vflash_renderer_set_clip(nr, cmd->data.cliprect.rect.x, cmd->data.cliprect.rect.y,
                                         cmd->data.cliprect.rect.w, cmd->data.cliprect.rect.h);
            } else {
                vflash_renderer_reset_clip(nr);
            }
            break;

        case SDL_RENDERCMD_CLEAR: {
            uint32_t rgb = ((uint32_t)cmd->data.color.r << 16) |
                           ((uint32_t)cmd->data.color.g << 8) |
                           (uint32_t)cmd->data.color.b;
            if (nr->has_target && rdata->current_target &&
                rdata->current_target->blendMode != SDL_BLENDMODE_NONE) {
                if (cmd->data.color.a == 0) {
                    rgb = 0;
                } else if (rgb == 0) {
                    rgb = 0x080808u;
                }
            }
            if (!nr->has_target) {
                int32_t sx = nr->clip_x, sy = nr->clip_y, sw = nr->clip_w, sh = nr->clip_h;
                vflash_gpu_clip(nr->current, 0, 0, (int32_t)nr->config.playfield_width, (int32_t)nr->config.playfield_height);
                vflash_renderer_clear(nr, rgb);
                vflash_gpu_clip(nr->current, sx, sy, sw, sh);
            } else {
                vflash_renderer_clear(nr, rgb);
            }
            break;
        }

        case SDL_RENDERCMD_DRAW_POINTS: {
            const SDL_FPoint *pts = (const SDL_FPoint *)((const uint8_t *)vertices + cmd->data.draw.first);
            uint32_t rgb = ((uint32_t)cmd->data.draw.r << 16) |
                           ((uint32_t)cmd->data.draw.g << 8) |
                           (uint32_t)cmd->data.draw.b;
            if (nr->has_target && rdata->current_target &&
                rdata->current_target->blendMode != SDL_BLENDMODE_NONE) {
                if (cmd->data.draw.a == 0) rgb = 0;
                else if (rgb == 0) rgb = 0x080808u;
            }
            for (size_t i = 0; i < cmd->data.draw.count; ++i) {
                vflash_renderer_fill_rect(nr, (int32_t)pts[i].x, (int32_t)pts[i].y, 1, 1, rgb);
            }
            break;
        }

        case SDL_RENDERCMD_DRAW_LINES: {
            const SDL_FPoint *pts = (const SDL_FPoint *)((const uint8_t *)vertices + cmd->data.draw.first);
            uint32_t rgb = ((uint32_t)cmd->data.draw.r << 16) |
                           ((uint32_t)cmd->data.draw.g << 8) |
                           (uint32_t)cmd->data.draw.b;
            if (nr->has_target && rdata->current_target &&
                rdata->current_target->blendMode != SDL_BLENDMODE_NONE) {
                if (cmd->data.draw.a == 0) rgb = 0;
                else if (rgb == 0) rgb = 0x080808u;
            }
            for (size_t i = 0; i + 1 < cmd->data.draw.count; ++i) {
                int32_t x0 = (int32_t)pts[i].x, y0 = (int32_t)pts[i].y;
                int32_t x1 = (int32_t)pts[i + 1].x, y1 = (int32_t)pts[i + 1].y;
                int32_t dx = x1 >= x0 ? x1 - x0 : x0 - x1;
                int32_t dy = y1 >= y0 ? y1 - y0 : y0 - y1;
                if (dy == 0) {
                    int32_t lx = x0 < x1 ? x0 : x1;
                    vflash_renderer_fill_rect(nr, lx, y0, dx + 1, 1, rgb);
                } else if (dx == 0) {
                    int32_t ty = y0 < y1 ? y0 : y1;
                    vflash_renderer_fill_rect(nr, x0, ty, 1, dy + 1, rgb);
                } else {
                    int32_t sx = x0 < x1 ? 1 : -1;
                    int32_t sy = y0 < y1 ? 1 : -1;
                    int32_t err = dx - dy;
                    for (;;) {
                        vflash_renderer_fill_rect(nr, x0, y0, 1, 1, rgb);
                        if (x0 == x1 && y0 == y1) break;
                        int32_t e2 = 2 * err;
                        if (e2 > -dy) { err -= dy; x0 += sx; }
                        if (e2 < dx) { err += dx; y0 += sy; }
                    }
                }
            }
            break;
        }

        case SDL_RENDERCMD_FILL_RECTS: {
            const SDL_FRect *rects = (const SDL_FRect *)((const uint8_t *)vertices + cmd->data.draw.first);
            uint32_t rgb = ((uint32_t)cmd->data.draw.r << 16) |
                           ((uint32_t)cmd->data.draw.g << 8) |
                           (uint32_t)cmd->data.draw.b;
            if (nr->has_target && rdata->current_target &&
                rdata->current_target->blendMode != SDL_BLENDMODE_NONE) {
                if (cmd->data.draw.a == 0) rgb = 0;
                else if (rgb == 0) rgb = 0x080808u;
            }
            for (size_t i = 0; i < cmd->data.draw.count; ++i) {
                vflash_renderer_fill_rect(nr, (int32_t)rects[i].x, (int32_t)rects[i].y,
                                          (int32_t)rects[i].w, (int32_t)rects[i].h, rgb);
            }
            break;
        }

        case SDL_RENDERCMD_COPY: {
            const VFLASH_CopyCmd *copy = (const VFLASH_CopyCmd *)((const uint8_t *)vertices + cmd->data.draw.first);
            SDL_Texture *tex = cmd->data.draw.texture;
            if (tex && tex->driverdata) {
                VFLASH_TextureData *tdata = (VFLASH_TextureData *)tex->driverdata;
                if (tdata->uploaded) {
                    if (tdata->is_target) {
                        tdata->native_tex.type = (tex->blendMode == SDL_BLENDMODE_NONE)
                            ? VFLASH_TEXTURE_OPAQUE
                            : VFLASH_TEXTURE_KEYED;
                    }
                    uint8_t alpha = (tex->blendMode == SDL_BLENDMODE_BLEND) ? tex->color.a : 255u;
                    uint32_t color_mod = ((uint32_t)tex->color.r << 16) |
                                         ((uint32_t)tex->color.g << 8) |
                                         (uint32_t)tex->color.b;
                    int32_t dw = (int32_t)(copy->dst.w + 0.5f);
                    int32_t dh = (int32_t)(copy->dst.h + 0.5f);
                    if (dw == copy->src.w && dh == copy->src.h &&
                        alpha == 255u && color_mod == 0xFFFFFFu) {
                        vflash_renderer_draw_sub(
                            nr, &tdata->native_tex,
                            copy->src.x, copy->src.y, copy->src.w, copy->src.h,
                            (int32_t)copy->dst.x, (int32_t)copy->dst.y);
                    } else {
                        vflash_sprite_transform_t xf;
                        xf.dx = (int32_t)copy->dst.x;
                        xf.dy = (int32_t)copy->dst.y;
                        xf.dw = dw;
                        xf.dh = dh;
                        xf.pivot_x = 0;
                        xf.pivot_y = 0;
                        xf.angle_deg16 = 0;
                        xf.flip = VFLASH_FLIP_NONE;
                        xf.alpha = alpha;
                        xf.color_mod = color_mod;
                        vflash_renderer_draw_sub_transformed(
                            nr, &tdata->native_tex,
                            copy->src.x, copy->src.y, copy->src.w, copy->src.h, &xf);
                    }
                }
            }
            break;
        }

        case SDL_RENDERCMD_COPY_EX: {
            const VFLASH_CopyExCmd *copy = (const VFLASH_CopyExCmd *)((const uint8_t *)vertices + cmd->data.draw.first);
            SDL_Texture *tex = cmd->data.draw.texture;
            if (tex && tex->driverdata) {
                VFLASH_TextureData *tdata = (VFLASH_TextureData *)tex->driverdata;
                if (tdata->uploaded) {
                    if (tdata->is_target) {
                        tdata->native_tex.type = (tex->blendMode == SDL_BLENDMODE_NONE)
                            ? VFLASH_TEXTURE_OPAQUE
                            : VFLASH_TEXTURE_KEYED;
                    }
                    uint8_t alpha = (tex->blendMode == SDL_BLENDMODE_BLEND) ? tex->color.a : 255u;
                    uint32_t color_mod = ((uint32_t)tex->color.r << 16) |
                                         ((uint32_t)tex->color.g << 8) |
                                         (uint32_t)tex->color.b;
                    uint32_t vflip = VFLASH_FLIP_NONE;
                    if (copy->flip & SDL_FLIP_HORIZONTAL) vflip |= VFLASH_FLIP_HORIZONTAL;
                    if (copy->flip & SDL_FLIP_VERTICAL) vflip |= VFLASH_FLIP_VERTICAL;

                    vflash_sprite_transform_t xf;
                    xf.dx = (int32_t)(copy->dst.x + (copy->dst.x >= 0.0f ? 0.5f : -0.5f));
                    xf.dy = (int32_t)(copy->dst.y + (copy->dst.y >= 0.0f ? 0.5f : -0.5f));
                    xf.dw = (int32_t)(copy->dst.w + 0.5f);
                    xf.dh = (int32_t)(copy->dst.h + 0.5f);
                    xf.pivot_x = (int32_t)(copy->center.x + (copy->center.x >= 0.0f ? 0.5f : -0.5f));
                    xf.pivot_y = (int32_t)(copy->center.y + (copy->center.y >= 0.0f ? 0.5f : -0.5f));
                    xf.angle_deg16 = (int32_t)(copy->angle * 65536.0);
                    xf.flip = vflip;
                    xf.alpha = alpha;
                    xf.color_mod = color_mod;
                    vflash_renderer_draw_sub_transformed(
                        nr, &tdata->native_tex,
                        copy->src.x, copy->src.y, copy->src.w, copy->src.h, &xf);
                }
            }
            break;
        }

        case SDL_RENDERCMD_GEOMETRY: {
            const VFLASH_GeomVert *verts = (const VFLASH_GeomVert *)((const uint8_t *)vertices + cmd->data.draw.first);
            size_t count = cmd->data.draw.count;
            SDL_Texture *tex = cmd->data.draw.texture;
            if (tex && tex->driverdata) {
                VFLASH_TextureData *tdata = (VFLASH_TextureData *)tex->driverdata;
                if (tdata->uploaded) {
                    if (tdata->is_target) {
                        tdata->native_tex.type = (tex->blendMode == SDL_BLENDMODE_NONE)
                            ? VFLASH_TEXTURE_OPAQUE
                            : VFLASH_TEXTURE_KEYED;
                    }
                    uint8_t base_a = (tex->blendMode == SDL_BLENDMODE_BLEND) ? tex->color.a : 255u;
                    for (size_t i = 0; i + 2 < count; i += 3) {
                        vflash_gpu_quad_vertex_t tri[3];
                        for (int k = 0; k < 3; ++k) {
                            tri[k].x = (int32_t)(verts[i + k].x * 65536.0f);
                            tri[k].y = (int32_t)(verts[i + k].y * 65536.0f);
                            tri[k].u = (int32_t)(verts[i + k].u * 65536.0f);
                            tri[k].v = (int32_t)(verts[i + k].v * 65536.0f);
                        }
                        uint8_t va = verts[i].color.a;
                        uint8_t alpha = (uint8_t)(((uint32_t)base_a * (uint32_t)va) / 255u);
                        uint32_t mr = ((uint32_t)tex->color.r * (uint32_t)verts[i].color.r) / 255u;
                        uint32_t mg = ((uint32_t)tex->color.g * (uint32_t)verts[i].color.g) / 255u;
                        uint32_t mb = ((uint32_t)tex->color.b * (uint32_t)verts[i].color.b) / 255u;
                        uint32_t color_mod = (mr << 16) | (mg << 8) | mb;
                        vflash_renderer_draw_tri_tex(
                            nr, &tdata->native_tex, tri, alpha, color_mod);
                    }
                }
                break;
            }
            int keyed = (nr->has_target && rdata->current_target &&
                         rdata->current_target->blendMode != SDL_BLENDMODE_NONE);
            for (size_t i = 0; i + 2 < count; i += 3) {
                SDL_Color col = verts[i].color;
                uint32_t rgb = ((uint32_t)col.r << 16) | ((uint32_t)col.g << 8) | (uint32_t)col.b;
                if (keyed) {
                    if (col.a == 0) {
                        rgb = 0;
                    } else if (rgb == 0) {
                        rgb = 0x080808u;
                    }
                }
                draw_solid_triangle(nr, &verts[i], &verts[i + 1], &verts[i + 2], rgb);
            }
            break;
        }

        default:
            break;
        }
        cmd = cmd->next;
    }
    return 0;
}

static int VFLASH_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect,
                                   Uint32 format, void *pixels, int pitch)
{
    (void)renderer;
    (void)rect;
    (void)format;
    (void)pixels;
    (void)pitch;
    SDL_SetError("RenderReadPixels not supported on hardware renderer");
    return -1;
}

extern void VFLASH_AudioPump(void);

static int VFLASH_RenderPresent(SDL_Renderer *renderer)
{
    VFLASH_AudioPump();
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    if (!rdata || !rdata->initialized) {
        return -1;
    }
    vflash_gpu_result_t res = vflash_renderer_present(&rdata->native_renderer);
    return (res == VFLASH_GPU_OK) ? 0 : -1;
}

static void VFLASH_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    if (texture && texture->driverdata) {
        VFLASH_TextureData *tdata = (VFLASH_TextureData *)texture->driverdata;
        if (tdata->uploaded && rdata && rdata->initialized) {
            vflash_renderer_destroy_texture(&rdata->native_renderer, &tdata->native_tex);
        }
        SDL_free(tdata);
        texture->driverdata = NULL;
    }
}

static void VFLASH_DestroyRenderer(SDL_Renderer *renderer)
{
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    if (rdata) {
        if (rdata->initialized) {
            vflash_renderer_shutdown(&rdata->native_renderer);
            rdata->initialized = 0;
        }
        SDL_free(rdata);
        renderer->driverdata = NULL;
    }
}

static int VFLASH_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, Uint32 flags)
{
    (void)flags;
    (void)window;

    VFLASH_RenderData *rdata = (VFLASH_RenderData *)SDL_calloc(1, sizeof(VFLASH_RenderData));
    if (!rdata) {
        SDL_OutOfMemory();
        return -1;
    }

    vflash_renderer_config_t cfg = {
        .surface_a = (void *)s_surface_a,
        .surface_b = (void *)s_surface_b,
        .surface_bytes = sizeof(s_surface_a),
        .surface_width = 640,
        .surface_height = 240,
        .playfield_width = 320,
        .playfield_height = 240,
        .screen_x = 0,
        .screen_y = 0,
        .atlas_x = 320,
        .atlas_y = 0,
        .atlas_width = 320,
        .atlas_height = 240
    };

    vflash_gpu_result_t res = vflash_renderer_init(&rdata->native_renderer, &cfg);
    if (res != VFLASH_GPU_OK) {
        SDL_free(rdata);
        SDL_SetError("failed to initialize native Zevio 320x240 renderer");
        return -1;
    }

    rdata->initialized = 1;

    renderer->WindowEvent = VFLASH_WindowEvent;
    renderer->GetOutputSize = VFLASH_GetOutputSize;
    renderer->CreateTexture = VFLASH_CreateTexture;
    renderer->UpdateTexture = VFLASH_UpdateTexture;
    renderer->LockTexture = VFLASH_LockTexture;
    renderer->UnlockTexture = VFLASH_UnlockTexture;
    renderer->SetTextureScaleMode = VFLASH_SetTextureScaleMode;
    renderer->SetRenderTarget = VFLASH_SetRenderTarget;
    renderer->QueueSetViewport = VFLASH_QueueSetViewport;
    renderer->QueueSetDrawColor = VFLASH_QueueSetDrawColor;
    renderer->QueueDrawPoints = VFLASH_QueueDrawPoints;
    renderer->QueueDrawLines = VFLASH_QueueDrawLines;
    renderer->QueueFillRects = VFLASH_QueueFillRects;
    renderer->QueueCopy = VFLASH_QueueCopy;
    renderer->QueueCopyEx = VFLASH_QueueCopyEx;
    renderer->QueueGeometry = VFLASH_QueueGeometry;
    renderer->RunCommandQueue = VFLASH_RunCommandQueue;
    renderer->RenderReadPixels = VFLASH_RenderReadPixels;
    renderer->RenderPresent = VFLASH_RenderPresent;
    renderer->DestroyTexture = VFLASH_DestroyTexture;
    renderer->DestroyRenderer = VFLASH_DestroyRenderer;

    renderer->info = VFLASH_RenderDriver.info;
    renderer->driverdata = rdata;
    return 0;
}

SDL_RenderDriver VFLASH_RenderDriver = {
    VFLASH_CreateRenderer,
    {
        "zevio",
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        8,
        {
            SDL_PIXELFORMAT_BGR555,
            SDL_PIXELFORMAT_RGB555,
            SDL_PIXELFORMAT_ABGR4444,
            SDL_PIXELFORMAT_ARGB4444,
            SDL_PIXELFORMAT_RGBA4444,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_PIXELFORMAT_ABGR8888
        },
        320,
        240
    }
};

// Diagnostics and introspection
vflash_renderer_t *vflash_sdl2_get_native_renderer(SDL_Renderer *renderer)
{
    if (!renderer) return NULL;
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    return rdata ? &rdata->native_renderer : NULL;
}

void *vflash_sdl2_get_surface_a(void)
{
    return (void *)s_surface_a;
}

void *vflash_sdl2_get_surface_b(void)
{
    return (void *)s_surface_b;
}

int vflash_sdl2_get_stats(SDL_Renderer *renderer, uint32_t *lists, uint32_t *sub_ticks, uint32_t *comp_ticks)
{
    if (!renderer) return -1;
    VFLASH_RenderData *rdata = (VFLASH_RenderData *)renderer->driverdata;
    if (!rdata || !rdata->initialized) return -1;
    vflash_gpu_stats_t s;
    vflash_gpu_result_t res = vflash_gpu_stats(&rdata->native_renderer.gpu, &s);
    if (res != VFLASH_GPU_OK) return -1;
    if (lists) *lists = s.lists;
    if (sub_ticks) *sub_ticks = s.submission_ticks;
    if (comp_ticks) *comp_ticks = s.completion_ticks;
    return 0;
}
