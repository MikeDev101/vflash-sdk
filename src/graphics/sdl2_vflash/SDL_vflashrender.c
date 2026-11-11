#include "SDL_internal.h"
#include "SDL_hints.h"
#include "render/SDL_sysrender.h"

#include "vflash/renderer.h"
#include "vflash/video.h"
#include "vflash/draw.h"

/* Static backing buffers for full 320x240 double-buffering with offscreen atlas */
static uint16_t s_surface_a[245760] __attribute__((aligned(64)));
static uint16_t s_surface_b[245760] __attribute__((aligned(64)));
static uint16_t s_conversion_buf[320 * 240] __attribute__((aligned(64)));

typedef struct {
    vflash_renderer_t native_renderer;
    int initialized;
} VFLASH_RenderData;

typedef struct {
    vflash_texture_t native_tex;
    int uploaded;
    vflash_texture_type_t native_type;
} VFLASH_TextureData;

typedef struct {
    SDL_Rect src;
    SDL_FRect dst;
} VFLASH_CopyCmd;

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
    (void)renderer;
    if (texture->w <= 0 || texture->h <= 0 || (texture->w & 1) != 0 ||
        texture->w > 320 || texture->h > 240) {
        SDL_SetError("unsupported texture dimensions");
        return -1;
    }

    VFLASH_TextureData *tdata = (VFLASH_TextureData *)SDL_calloc(1, sizeof(VFLASH_TextureData));
    if (!tdata) {
        SDL_OutOfMemory();
        return -1;
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

    vflash_gpu_result_t res = vflash_renderer_upload_texture(&rdata->native_renderer, &tdata->native_tex,
                                                             dst, (uint32_t)texture->w, (uint32_t)texture->h, native_type);
    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to upload texture to atlas");
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
    (void)renderer;
    if (texture != NULL) {
        SDL_SetError("render-to-texture unsupported");
        return -1;
    }
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
    (void)renderer;
    (void)cmd;
    (void)points;
    (void)count;
    return 0;
}

static int VFLASH_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                                 const SDL_FPoint *points, int count)
{
    (void)renderer;
    (void)cmd;
    (void)points;
    (void)count;
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
    /* Reject scaling */
    if ((int)dstrect->w != srcrect->w || (int)dstrect->h != srcrect->h) {
        SDL_SetError("unsupported scaling in SDL_RenderCopy (source dimensions must equal destination dimensions)");
        return -1;
    }

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
            int32_t sx = nr->clip_x, sy = nr->clip_y, sw = nr->clip_w, sh = nr->clip_h;
            vflash_gpu_clip(nr->current, 0, 0, (int32_t)nr->config.playfield_width, (int32_t)nr->config.playfield_height);
            vflash_renderer_clear(nr, rgb);
            vflash_gpu_clip(nr->current, sx, sy, sw, sh);
            break;
        }

        case SDL_RENDERCMD_FILL_RECTS: {
            const SDL_FRect *rects = (const SDL_FRect *)((const uint8_t *)vertices + cmd->data.draw.first);
            uint32_t rgb = ((uint32_t)cmd->data.draw.r << 16) |
                           ((uint32_t)cmd->data.draw.g << 8) |
                           (uint32_t)cmd->data.draw.b;
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
                    vflash_renderer_draw_sub(nr, &tdata->native_tex,
                                             copy->src.x, copy->src.y, copy->src.w, copy->src.h,
                                             (int32_t)copy->dst.x, (int32_t)copy->dst.y);
                }
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
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
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

/* Diagnostics and introspection */
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
