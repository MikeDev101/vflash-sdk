#include "vflash/sdl_render.h"

static char g_sdl_error[128];

const char *SDL_GetError(void)
{
    return g_sdl_error;
}

void SDL_ClearError(void)
{
    g_sdl_error[0] = '\0';
}

void SDL_SetError(const char *msg)
{
    if (!msg) {
        g_sdl_error[0] = '\0';
        return;
    }
    uint32_t i = 0;
    while (i < sizeof(g_sdl_error) - 1 && msg[i]) {
        g_sdl_error[i] = msg[i];
        i++;
    }
    g_sdl_error[i] = '\0';
}

SDL_Renderer *SDL_CreateRenderer_Static(SDL_Renderer *storage, const vflash_renderer_config_t *config)
{
    if (!storage || !config) {
        SDL_SetError("invalid arguments to SDL_CreateRenderer_Static");
        return NULL;
    }
    vflash_gpu_result_t res = vflash_renderer_init(&storage->native_renderer, config);
    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to initialize native renderer");
        return NULL;
    }
    storage->draw_color.r = 0;
    storage->draw_color.g = 0;
    storage->draw_color.b = 0;
    storage->draw_color.a = 255;
    storage->clip_enabled = 0;
    storage->clip_rect.x = 0;
    storage->clip_rect.y = 0;
    storage->clip_rect.w = (int)config->playfield_width;
    storage->clip_rect.h = (int)config->playfield_height;

    for (uint32_t i = 0; i < SDL_MAX_TEXTURES; ++i) {
        storage->textures[i].in_use = 0;
        storage->textures[i].uploaded = 0;
        storage->textures[i].renderer = NULL;
    }
    storage->initialized = 1;
    SDL_ClearError();
    return storage;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    if (!renderer || !renderer->initialized) return;
    for (uint32_t i = 0; i < SDL_MAX_TEXTURES; ++i) {
        if (renderer->textures[i].in_use && renderer->textures[i].uploaded) {
            vflash_renderer_destroy_texture(&renderer->native_renderer, &renderer->textures[i].native_tex);
        }
        renderer->textures[i].in_use = 0;
        renderer->textures[i].uploaded = 0;
    }
    vflash_renderer_shutdown(&renderer->native_renderer);
    renderer->initialized = 0;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, uint32_t format, int access, int w, int h)
{
    if (!renderer || !renderer->initialized) {
        SDL_SetError("renderer not initialized");
        return NULL;
    }
    if (w <= 0 || h <= 0 || (w & 1)) {
        SDL_SetError("invalid texture dimensions (must be positive, width must be even)");
        return NULL;
    }
    if (access != SDL_TEXTUREACCESS_STATIC) {
        SDL_SetError("unsupported texture access (only STATIC is supported)");
        return NULL;
    }
    switch (format) {
    case SDL_PIXELFORMAT_RGB555:
    case SDL_PIXELFORMAT_BGR555:
    case SDL_PIXELFORMAT_ARGB4444:
    case SDL_PIXELFORMAT_RGBA4444:
    case SDL_PIXELFORMAT_ABGR4444:
    case SDL_PIXELFORMAT_ARGB8888:
    case SDL_PIXELFORMAT_RGBA8888:
    case SDL_PIXELFORMAT_ABGR8888:
        break;
    default:
        SDL_SetError("unsupported pixel format");
        return NULL;
    }

    SDL_Texture *tex = NULL;
    for (uint32_t i = 0; i < SDL_MAX_TEXTURES; ++i) {
        if (!renderer->textures[i].in_use) {
            tex = &renderer->textures[i];
            break;
        }
    }
    if (!tex) {
        SDL_SetError("out of texture descriptors");
        return NULL;
    }

    tex->renderer = renderer;
    tex->format = format;
    tex->access = access;
    tex->w = w;
    tex->h = h;
    tex->uploaded = 0;
    tex->in_use = 1;

    // Default blend mode
    if (format == SDL_PIXELFORMAT_RGB555 || format == SDL_PIXELFORMAT_BGR555) {
        tex->blend_mode = SDL_BLENDMODE_NONE;
    } else {
        tex->blend_mode = SDL_BLENDMODE_BLEND;
    }
    SDL_ClearError();
    return tex;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (!texture || !texture->in_use) return;
    if (texture->uploaded && texture->renderer && texture->renderer->initialized) {
        vflash_renderer_destroy_texture(&texture->renderer->native_renderer, &texture->native_tex);
    }
    texture->in_use = 0;
    texture->uploaded = 0;
    texture->renderer = NULL;
}

int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode)
{
    if (!texture || !texture->in_use) {
        SDL_SetError("invalid texture");
        return -1;
    }
    if (texture->uploaded) {
        if (texture->blend_mode != blendMode) {
            SDL_SetError("cannot change blend mode on uploaded static texture");
            return -1;
        }
        return 0;
    }
    texture->blend_mode = blendMode;
    SDL_ClearError();
    return 0;
}

int SDL_GetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode *blendMode)
{
    if (!texture || !texture->in_use || !blendMode) {
        SDL_SetError("invalid arguments to SDL_GetTextureBlendMode");
        return -1;
    }
    *blendMode = texture->blend_mode;
    return 0;
}

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    if (!texture || !texture->in_use || !pixels) {
        SDL_SetError("invalid arguments to SDL_UpdateTexture");
        return -1;
    }
    SDL_Renderer *r = texture->renderer;
    if (!r || !r->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    // Reject partial updates
    if (rect != NULL) {
        if (rect->x != 0 || rect->y != 0 || rect->w != texture->w || rect->h != texture->h) {
            SDL_SetError("partial texture update unsupported");
            return -1;
        }
    }
    // Reject live texture updates during active double-buffering
    if (r->native_renderer.frame_count > 0 && vflash_video_tiled_address() != 0) {
        SDL_SetError("live texture update rejected during tiled scanout");
        return -1;
    }

    uint32_t total_pixels = (uint32_t)texture->w * (uint32_t)texture->h;
    if (total_pixels > SDL_CONVERSION_PIXELS) {
        SDL_SetError("texture exceeds maximum conversion buffer capacity");
        return -1;
    }

    vflash_texture_type_t native_type;
    uint16_t *dst = r->conversion_buf;

    if (texture->blend_mode == SDL_BLENDMODE_BLEND) {
        native_type = VFLASH_TEXTURE_TRANSLUCENT;
        if (texture->format == SDL_PIXELFORMAT_ARGB8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint32_t a = (p >> 24) & 0xFF;
                    uint32_t red = (p >> 16) & 0xFF;
                    uint32_t green = (p >> 8) & 0xFF;
                    uint32_t blue = p & 0xFF;
                    uint16_t a4 = (uint16_t)(a >> 4);
                    uint16_t r4 = (uint16_t)(red >> 4);
                    uint16_t g4 = (uint16_t)(green >> 4);
                    uint16_t b4 = (uint16_t)(blue >> 4);
                    dst[y * texture->w + x] = (a4 << 12) | (b4 << 8) | (g4 << 4) | r4;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_RGBA8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint32_t red = (p >> 24) & 0xFF;
                    uint32_t green = (p >> 16) & 0xFF;
                    uint32_t blue = (p >> 8) & 0xFF;
                    uint32_t a = p & 0xFF;
                    uint16_t a4 = (uint16_t)(a >> 4);
                    uint16_t r4 = (uint16_t)(red >> 4);
                    uint16_t g4 = (uint16_t)(green >> 4);
                    uint16_t b4 = (uint16_t)(blue >> 4);
                    dst[y * texture->w + x] = (a4 << 12) | (b4 << 8) | (g4 << 4) | r4;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_ABGR8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint32_t a = (p >> 24) & 0xFF;
                    uint32_t blue = (p >> 16) & 0xFF;
                    uint32_t green = (p >> 8) & 0xFF;
                    uint32_t red = p & 0xFF;
                    uint16_t a4 = (uint16_t)(a >> 4);
                    uint16_t r4 = (uint16_t)(red >> 4);
                    uint16_t g4 = (uint16_t)(green >> 4);
                    uint16_t b4 = (uint16_t)(blue >> 4);
                    dst[y * texture->w + x] = (a4 << 12) | (b4 << 8) | (g4 << 4) | r4;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_ARGB4444) {
            for (int y = 0; y < texture->h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint16_t p = row[x];
                    uint16_t a4 = (p >> 12) & 15;
                    uint16_t r4 = (p >> 8) & 15;
                    uint16_t g4 = (p >> 4) & 15;
                    uint16_t b4 = p & 15;
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
        native_type = VFLASH_TEXTURE_OPAQUE;
        if (texture->format == SDL_PIXELFORMAT_ARGB8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint16_t r5 = (uint16_t)((p >> 19) & 31);
                    uint16_t g5 = (uint16_t)((p >> 11) & 31);
                    uint16_t b5 = (uint16_t)((p >> 3) & 31);
                    dst[y * texture->w + x] = (b5 << 10) | (g5 << 5) | r5;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_RGBA8888) {
            for (int y = 0; y < texture->h; ++y) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint32_t p = row[x];
                    uint16_t r5 = (uint16_t)((p >> 27) & 31);
                    uint16_t g5 = (uint16_t)((p >> 19) & 31);
                    uint16_t b5 = (uint16_t)((p >> 11) & 31);
                    dst[y * texture->w + x] = (b5 << 10) | (g5 << 5) | r5;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_RGB555) {
            for (int y = 0; y < texture->h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    uint16_t p = row[x];
                    uint16_t r5 = (p >> 10) & 31;
                    uint16_t g5 = (p >> 5) & 31;
                    uint16_t b5 = p & 31;
                    dst[y * texture->w + x] = (b5 << 10) | (g5 << 5) | r5;
                }
            }
        } else if (texture->format == SDL_PIXELFORMAT_BGR555) {
            for (int y = 0; y < texture->h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)pixels + y * pitch);
                for (int x = 0; x < texture->w; ++x) {
                    dst[y * texture->w + x] = row[x] & 0x7FFF;
                }
            }
        } else {
            SDL_SetError("unsupported format for blend mode NONE");
            return -1;
        }
    }

    vflash_gpu_result_t res = vflash_renderer_upload_texture(&r->native_renderer, &texture->native_tex,
                                                            dst, (uint32_t)texture->w, (uint32_t)texture->h, native_type);
    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to upload texture to native atlas");
        return -1;
    }
    texture->uploaded = 1;
    SDL_ClearError();
    return 0;
}

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (!renderer || !renderer->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    renderer->draw_color.r = r;
    renderer->draw_color.g = g;
    renderer->draw_color.b = b;
    renderer->draw_color.a = a;
    return 0;
}

int SDL_GetRenderDrawColor(SDL_Renderer *renderer, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    if (!renderer || !renderer->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    if (r) *r = renderer->draw_color.r;
    if (g) *g = renderer->draw_color.g;
    if (b) *b = renderer->draw_color.b;
    if (a) *a = renderer->draw_color.a;
    return 0;
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    if (!renderer || !renderer->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    vflash_renderer_t *nr = &renderer->native_renderer;
    uint32_t rgb = ((uint32_t)renderer->draw_color.r << 16) |
                   ((uint32_t)renderer->draw_color.g << 8) |
                   (uint32_t)renderer->draw_color.b;

    // SDL2 RenderClear ignores the active clip rectangle
    int32_t saved_x = nr->clip_x;
    int32_t saved_y = nr->clip_y;
    int32_t saved_w = nr->clip_w;
    int32_t saved_h = nr->clip_h;

    vflash_gpu_clip(nr->current, 0, 0, (int32_t)nr->config.playfield_width, (int32_t)nr->config.playfield_height);
    vflash_gpu_result_t res = vflash_renderer_clear(nr, rgb);
    vflash_gpu_clip(nr->current, saved_x, saved_y, saved_w, saved_h);

    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to clear render target");
        return -1;
    }
    return 0;
}

int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    if (!renderer || !renderer->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    int rx = 0, ry = 0;
    int rw = (int)renderer->native_renderer.config.playfield_width;
    int rh = (int)renderer->native_renderer.config.playfield_height;
    if (rect != NULL) {
        rx = rect->x;
        ry = rect->y;
        rw = rect->w;
        rh = rect->h;
    }
    if (rw <= 0 || rh <= 0) return 0;
    uint32_t rgb = ((uint32_t)renderer->draw_color.r << 16) |
                   ((uint32_t)renderer->draw_color.g << 8) |
                   (uint32_t)renderer->draw_color.b;
    vflash_gpu_result_t res = vflash_renderer_fill_rect(&renderer->native_renderer, rx, ry, rw, rh, rgb);
    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to fill rectangle");
        return -1;
    }
    return 0;
}

int SDL_RenderSetClipRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    if (!renderer || !renderer->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    if (rect == NULL || rect->w <= 0 || rect->h <= 0) {
        renderer->clip_enabled = 0;
        renderer->clip_rect.x = 0;
        renderer->clip_rect.y = 0;
        renderer->clip_rect.w = (int)renderer->native_renderer.config.playfield_width;
        renderer->clip_rect.h = (int)renderer->native_renderer.config.playfield_height;
        vflash_renderer_reset_clip(&renderer->native_renderer);
    } else {
        renderer->clip_enabled = 1;
        renderer->clip_rect = *rect;
        vflash_renderer_set_clip(&renderer->native_renderer, rect->x, rect->y, rect->w, rect->h);
    }
    return 0;
}

void SDL_RenderGetClipRect(SDL_Renderer *renderer, SDL_Rect *rect)
{
    if (!renderer || !rect) return;
    *rect = renderer->clip_rect;
}

int SDL_RenderIsClipEnabled(SDL_Renderer *renderer)
{
    return renderer ? renderer->clip_enabled : 0;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    if (!renderer || !renderer->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    if (!texture || !texture->in_use || !texture->uploaded) {
        SDL_SetError("invalid or un-uploaded texture");
        return -1;
    }
    int sx = 0, sy = 0, sw = texture->w, sh = texture->h;
    if (srcrect != NULL) {
        sx = srcrect->x;
        sy = srcrect->y;
        sw = srcrect->w;
        sh = srcrect->h;
    }
    int dx = 0, dy = 0, dw = (int)renderer->native_renderer.config.playfield_width;
    int dh = (int)renderer->native_renderer.config.playfield_height;
    if (dstrect != NULL) {
        dx = dstrect->x;
        dy = dstrect->y;
        dw = dstrect->w;
        dh = dstrect->h;
    }
    // Explicitly reject unsupported scaling
    if (sw != dw || sh != dh) {
        SDL_SetError("unsupported scaling in SDL_RenderCopy (source dimensions must equal destination dimensions)");
        return -1;
    }
    // Validate source rectangle bounds
    if (sx < 0 || sy < 0 || sw < 0 || sh < 0 ||
        (uint32_t)sx + (uint32_t)sw > (uint32_t)texture->w ||
        (uint32_t)sy + (uint32_t)sh > (uint32_t)texture->h) {
        SDL_SetError("source rect outside texture bounds");
        return -1;
    }
    if (sw == 0 || sh == 0) return 0;

    vflash_gpu_result_t res = vflash_renderer_draw_sub(&renderer->native_renderer, &texture->native_tex,
                                                      sx, sy, sw, sh, dx, dy);
    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to draw sprite via native renderer");
        return -1;
    }
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    if (!renderer || !renderer->initialized) return;
    vflash_renderer_present(&renderer->native_renderer);
}

int SDL_RenderResetAtlas(SDL_Renderer *renderer)
{
    if (!renderer || !renderer->initialized) {
        SDL_SetError("renderer not initialized");
        return -1;
    }
    for (uint32_t i = 0; i < SDL_MAX_TEXTURES; ++i) {
        renderer->textures[i].uploaded = 0;
        renderer->textures[i].in_use = 0;
    }
    vflash_gpu_result_t res = vflash_renderer_reset_atlas(&renderer->native_renderer);
    if (res != VFLASH_GPU_OK) {
        SDL_SetError("failed to reset atlas");
        return -1;
    }
    return 0;
}

