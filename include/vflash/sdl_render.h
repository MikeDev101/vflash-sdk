#ifndef VFLASH_SDL_RENDER_H
#define VFLASH_SDL_RENDER_H

#include "vflash/renderer.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

typedef struct {
    int x, y, w, h;
} SDL_Rect;

typedef struct {
    uint8_t r, g, b, a;
} SDL_Color;

/* Standard SDL2 32-bit pixel format constants */
typedef enum {
    SDL_PIXELFORMAT_UNKNOWN  = 0,
    SDL_PIXELFORMAT_RGB555   = 0x12071002,
    SDL_PIXELFORMAT_BGR555   = 0x12081002,
    SDL_PIXELFORMAT_ARGB4444 = 0x13021002,
    SDL_PIXELFORMAT_RGBA4444 = 0x13031002,
    SDL_PIXELFORMAT_ABGR4444 = 0x13041002,
    SDL_PIXELFORMAT_ARGB8888 = 0x16362004,
    SDL_PIXELFORMAT_RGBA8888 = 0x16462004,
    SDL_PIXELFORMAT_ABGR8888 = 0x16762004
} SDL_PixelFormatEnum;

typedef enum {
    SDL_TEXTUREACCESS_STATIC    = 0,
    SDL_TEXTUREACCESS_STREAMING = 1,
    SDL_TEXTUREACCESS_TARGET    = 2
} SDL_TextureAccess;

typedef enum {
    SDL_BLENDMODE_NONE  = 0x00000000,
    SDL_BLENDMODE_BLEND = 0x00000001
} SDL_BlendMode;

#define SDL_MAX_TEXTURES 16
#define SDL_CONVERSION_PIXELS 2048

struct SDL_Texture {
    SDL_Renderer *renderer;
    uint32_t format;
    int access;
    int w;
    int h;
    SDL_BlendMode blend_mode;
    int uploaded;
    vflash_texture_t native_tex;
    int in_use;
};

struct SDL_Renderer {
    vflash_renderer_t native_renderer;
    SDL_Color draw_color;
    SDL_Rect clip_rect;
    int clip_enabled;
    SDL_Texture textures[SDL_MAX_TEXTURES];
    uint16_t conversion_buf[SDL_CONVERSION_PIXELS] __attribute__((aligned(64)));
    int initialized;
};

/* Core SDL2 rendering API subset */
SDL_Renderer *SDL_CreateRenderer_Static(SDL_Renderer *storage, const vflash_renderer_config_t *config);
void SDL_DestroyRenderer(SDL_Renderer *renderer);

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, uint32_t format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture *texture);

int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode);
int SDL_GetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode *blendMode);

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
int SDL_GetRenderDrawColor(SDL_Renderer *renderer, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);

int SDL_RenderSetClipRect(SDL_Renderer *renderer, const SDL_Rect *rect);
void SDL_RenderGetClipRect(SDL_Renderer *renderer, SDL_Rect *rect);
int SDL_RenderIsClipEnabled(SDL_Renderer *renderer);

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect);
void SDL_RenderPresent(SDL_Renderer *renderer);

/* Extension helper for inter-scene atlas reset */
int SDL_RenderResetAtlas(SDL_Renderer *renderer);

/* Error handling */
const char *SDL_GetError(void);
void SDL_ClearError(void);
void SDL_SetError(const char *msg);

#ifdef __cplusplus
}
#endif

#endif
