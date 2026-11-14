// Hardware-accelerated GPU color modulation & alpha blending test.
// Tests white-to-black, grayscale, arbitrary RGB tinting, and alpha blending
// across SDL_RenderCopyEx and textured SDL_RenderGeometry in FlashEm.

#include <SDL.h>
#include "vflash/clock.h"
#include "vflash/mmu.h"

#include <stdint.h>

volatile uint32_t boot_marker;
volatile uint32_t startup_status;
volatile uint32_t data_cookie = 0x13579BDFu;

static uint32_t white_pixels[16 * 16];

static void draw_swatch_copy_plain(SDL_Renderer *r, SDL_Texture *tex,
                                   int x, int y, uint8_t cr, uint8_t cg, uint8_t cb,
                                   uint8_t ca)
{
    SDL_SetTextureColorMod(tex, cr, cg, cb);
    SDL_SetTextureAlphaMod(tex, ca);
    SDL_Rect dst = { x, y, 24, 24 };
    SDL_RenderCopy(r, tex, NULL, &dst);
}

static void draw_swatch_copy(SDL_Renderer *r, SDL_Texture *tex,
                             int x, int y, uint8_t cr, uint8_t cg, uint8_t cb,
                             uint8_t ca)
{
    SDL_SetTextureColorMod(tex, cr, cg, cb);
    SDL_SetTextureAlphaMod(tex, ca);
    SDL_Rect dst = { x, y, 24, 24 };
    SDL_RenderCopyEx(r, tex, NULL, &dst, 0.0, NULL, SDL_FLIP_NONE);
}

static void draw_swatch_geom(SDL_Renderer *r, SDL_Texture *tex,
                             int x, int y, uint8_t cr, uint8_t cg, uint8_t cb,
                             uint8_t ca)
{
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_Vertex v[4];
    float x0 = (float)x, y0 = (float)y;
    float x1 = x0 + 24.0f, y1 = y0 + 24.0f;
    SDL_Color col = { cr, cg, cb, ca };
    v[0].position.x = x0; v[0].position.y = y0; v[0].color = col; v[0].tex_coord.x = 0.0f; v[0].tex_coord.y = 0.0f;
    v[1].position.x = x1; v[1].position.y = y0; v[1].color = col; v[1].tex_coord.x = 1.0f; v[1].tex_coord.y = 0.0f;
    v[2].position.x = x1; v[2].position.y = y1; v[2].color = col; v[2].tex_coord.x = 1.0f; v[2].tex_coord.y = 1.0f;
    v[3].position.x = x0; v[3].position.y = y1; v[3].color = col; v[3].tex_coord.x = 0.0f; v[3].tex_coord.y = 1.0f;
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, tex, v, 4, idx, 6);
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu)
        ? 0x600D600Du : 0;
    boot_marker = 0x56464C53u;

#ifdef GPU_SDL2_MMU
    vflash_mmu_init();
#endif
    vflash_clock_init();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return;
    }

    SDL_Window *win = SDL_CreateWindow(
        "color_mod_test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        320, 240, 0);
    if (!win) return;

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) return;

    for (int i = 0; i < 16 * 16; ++i) {
        white_pixels[i] = 0xFFFFFFFFu;
    }

    SDL_Texture *white_tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!white_tex) return;
    SDL_SetTextureBlendMode(white_tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(white_tex, NULL, white_pixels, 16 * 4);

    static const uint8_t palette[7][3] = {
        { 255, 255, 255 }, // White
        {   0,   0,   0 }, // Black
        { 128, 128, 128 }, // Gray
        { 255,   0,   0 }, // Red
        {   0, 255,   0 }, // Green
        {   0,   0, 255 }, // Blue
        {  48, 144, 240 }, // Arbitrary RGB (0x3090F0)
    };

    for (;;) {
        SDL_SetRenderDrawColor(ren, 64, 64, 64, 255);
        SDL_RenderClear(ren);

        // Background bars under rows 2 & 3 to verify alpha blending
        SDL_SetRenderDrawColor(ren, 220, 220, 40, 255);
        SDL_Rect bar1 = { 10, 96, 260, 12 };
        SDL_RenderFillRect(ren, &bar1);
        SDL_Rect bar2 = { 10, 140, 260, 12 };
        SDL_RenderFillRect(ren, &bar2);

        for (int i = 0; i < 7; ++i) {
            int x = 16 + i * 36;
            uint8_t r = palette[i][0];
            uint8_t g = palette[i][1];
            uint8_t b = palette[i][2];

            // Row 0: SDL_RenderCopyEx opaque
            draw_swatch_copy(ren, white_tex, x, 16, r, g, b, 255);
            // Row 1: SDL_RenderGeometry opaque
            draw_swatch_geom(ren, white_tex, x, 52, r, g, b, 255);
            // Row 2: SDL_RenderCopyEx alpha=128
            draw_swatch_copy(ren, white_tex, x, 88, r, g, b, 128);
            // Row 3: SDL_RenderGeometry alpha=128
            draw_swatch_geom(ren, white_tex, x, 132, r, g, b, 128);
            // Row 4: SDL_RenderCopy opaque
            draw_swatch_copy_plain(ren, white_tex, x, 176, r, g, b, 255);
        }

        SDL_RenderPresent(ren);
    }
}
