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

//
// Integer rectangle used by the SDL2-compatible rendering shim.
//
typedef struct
{
    int x;
    int y;
    int w;
    int h;
} SDL_Rect;

//
// 8-bit RGBA color tuple.
//
typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} SDL_Color;

//
// Standard SDL2 32-bit pixel format constants supported by the shim.
//
typedef enum
{
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

//
// Texture access pattern constants.
//
typedef enum
{
    SDL_TEXTUREACCESS_STATIC    = 0,
    SDL_TEXTUREACCESS_STREAMING = 1,
    SDL_TEXTUREACCESS_TARGET    = 2
} SDL_TextureAccess;

//
// Texture alpha blending modes.
//
typedef enum
{
    SDL_BLENDMODE_NONE  = 0x00000000,
    SDL_BLENDMODE_BLEND = 0x00000001
} SDL_BlendMode;

//
// Maximum texture slots and conversion scratch buffer size in pixels.
//
#define SDL_MAX_TEXTURES 16
#define SDL_CONVERSION_PIXELS 2048

//
// Static-slot texture object backed by a vflash_texture_t in the atlas.
//
struct SDL_Texture
{
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

//
// Static-storage SDL2-compatible renderer wrapping vflash_renderer_t.
//
struct SDL_Renderer
{
    vflash_renderer_t native_renderer;
    SDL_Color draw_color;
    SDL_Rect clip_rect;
    int clip_enabled;
    SDL_Texture textures[SDL_MAX_TEXTURES];
    uint16_t conversion_buf[SDL_CONVERSION_PIXELS] __attribute__((aligned(64)));
    int initialized;
};

//
// Initialize a caller-allocated SDL_Renderer backed by vflash_renderer_t.
//
// storage: Caller-owned SDL_Renderer storage.
// config:  Native renderer surface and atlas configuration.
//
// Returns storage on success, or NULL on failure.
//
SDL_Renderer *SDL_CreateRenderer_Static(SDL_Renderer *storage,
                                        const vflash_renderer_config_t *config);

//
// Shut down an SDL_Renderer and its underlying native renderer.
//
// renderer: Renderer instance to destroy.
//
void SDL_DestroyRenderer(SDL_Renderer *renderer);

//
// Allocate a texture slot associated with renderer.
//
// renderer: Active SDL_Renderer instance.
// format:   Source pixel format (SDL_PIXELFORMAT_*).
// access:   Texture access mode (SDL_TEXTUREACCESS_*).
// w:        Texture width in pixels.
// h:        Texture height in pixels.
//
// Returns a pointer to the allocated SDL_Texture, or NULL on failure.
//
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, uint32_t format,
                               int access, int w, int h);

//
// Release an SDL_Texture slot back to its owning renderer.
//
// texture: Texture handle to destroy.
//
void SDL_DestroyTexture(SDL_Texture *texture);

//
// Set the blend mode used when rendering texture.
//
// texture:   Target texture handle.
// blendMode: Blend mode (SDL_BLENDMODE_NONE or SDL_BLENDMODE_BLEND).
//
// Returns 0 on success, or -1 on error.
//
int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode);

//
// Query the blend mode currently assigned to texture.
//
// texture:   Target texture handle.
// blendMode: Output pointer receiving the active SDL_BlendMode.
//
// Returns 0 on success, or -1 on error.
//
int SDL_GetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode *blendMode);

//
// Convert and upload pixel data into a texture's atlas slot.
//
// texture: Target texture handle.
// rect:    Optional sub-rectangle (must be NULL or full texture).
// pixels:  Source pixel data in the texture's declared format.
// pitch:   Source row pitch in bytes.
//
// Returns 0 on success, or -1 on error.
//
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                      const void *pixels, int pitch);

//
// Set the active draw color used by SDL_RenderClear and SDL_RenderFillRect.
//
// renderer: Target renderer instance.
// r:        Red component (0..255).
// g:        Green component (0..255).
// b:        Blue component (0..255).
// a:        Alpha component (0..255).
//
// Returns 0 on success, or -1 if renderer is invalid.
//
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, uint8_t r, uint8_t g,
                           uint8_t b, uint8_t a);

//
// Query the active draw color of renderer.
//
// renderer: Target renderer instance.
// r:        Optional output pointer for red component.
// g:        Optional output pointer for green component.
// b:        Optional output pointer for blue component.
// a:        Optional output pointer for alpha component.
//
// Returns 0 on success, or -1 if renderer is invalid.
//
int SDL_GetRenderDrawColor(SDL_Renderer *renderer, uint8_t *r, uint8_t *g,
                           uint8_t *b, uint8_t *a);

//
// Clear the active playfield with the current draw color.
//
// renderer: Target renderer instance.
//
// Returns 0 on success, or -1 on error.
//
int SDL_RenderClear(SDL_Renderer *renderer);

//
// Fill a clipped rectangle on the playfield with the current draw color.
//
// renderer: Target renderer instance.
// rect:     Rectangle to fill, or NULL to fill the entire playfield.
//
// Returns 0 on success, or -1 on error.
//
int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);

//
// Set or clear the renderer clipping rectangle.
//
// renderer: Target renderer instance.
// rect:     Clip rectangle, or NULL to disable clipping.
//
// Returns 0 on success, or -1 on error.
//
int SDL_RenderSetClipRect(SDL_Renderer *renderer, const SDL_Rect *rect);

//
// Query the current renderer clipping rectangle.
//
// renderer: Target renderer instance.
// rect:     Output SDL_Rect receiving the active clip bounds.
//
void SDL_RenderGetClipRect(SDL_Renderer *renderer, SDL_Rect *rect);

//
// Query whether clipping is currently enabled on renderer.
//
// renderer: Target renderer instance.
//
// Returns 1 if clipping is enabled, or 0 otherwise.
//
int SDL_RenderIsClipEnabled(SDL_Renderer *renderer);

//
// Copy a texture or sub-rectangle to the playfield at dstrect.
//
// renderer: Target renderer instance.
// texture:  Uploaded texture to draw.
// srcrect:  Optional source rectangle (NULL for full texture).
// dstrect:  Optional destination rectangle (NULL for origin).
//
// Returns 0 on success, or -1 on error.
//
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect);

//
// Present the rendered frame and swap double buffers.
//
// renderer: Target renderer instance.
//
void SDL_RenderPresent(SDL_Renderer *renderer);

//
// Reset the underlying texture atlas and clear uploaded texture flags.
//
// renderer: Target renderer instance.
//
// Returns 0 on success, or -1 on error.
//
int SDL_RenderResetAtlas(SDL_Renderer *renderer);

//
// Retrieve the last SDL error string recorded by the shim.
//
// Returns a null-terminated error message string.
//
const char *SDL_GetError(void);

//
// Clear the last recorded SDL error message.
//
void SDL_ClearError(void);

//
// Set the current SDL error message string.
//
// msg: Null-terminated error description.
//
void SDL_SetError(const char *msg);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_SDL_RENDER_H
