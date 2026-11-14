// Hardware-accelerated sprite transformation demo and verification suite.
// Exercises SDL_RenderCopyEx, SDL_RenderCopy scaling, textured
// SDL_RenderGeometry, render targets, clipping, alpha/color mod, and edge
// cases with MMU OFF and MMU ON.

#include <SDL.h>
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/mmu.h"

#include <stdint.h>

volatile uint32_t startup_status;
volatile uint32_t transform_status;
volatile uint32_t transform_frames;
volatile uint32_t transform_errors;
volatile uint32_t transform_cp15_c1;

volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;
static uint32_t sprite_pixels[32 * 32];
static uint32_t alpha_pixels[32 * 32];

// Asymmetric 32x32 texture:
// TL=Red, TR=Green, BL=Blue, BR=Yellow, white border on top/left, diagonal
// white marker so orientation and flipping are unambiguous.
static void build_asymmetric_texture(uint32_t *buf, uint8_t alpha)
{
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            uint32_t c;
            if (y < 16 && x < 16) {
                c = 0x00FF2020u; // Red
            } else if (y < 16) {
                c = 0x0020FF20u; // Green
            } else if (x < 16) {
                c = 0x002060FFu; // Blue
            } else {
                c = 0x00FFFF20u; // Yellow
            }
            if (x == 0 || y == 0 || (x == y && x < 24)) {
                c = 0x00FFFFFFu; // Orientation notch
            }
            buf[y * 32 + x] = ((uint32_t)alpha << 24) | c;
        }
    }
}

static void render_scene(SDL_Renderer *r, SDL_Texture *tex,
                         SDL_Texture *alpha_tex, SDL_Texture *target_tex,
                         int frame)
{
    // 10. Render transformed sprite into offscreen render target
    if (SDL_SetRenderTarget(r, target_tex) != 0) transform_errors++;
    SDL_SetRenderDrawColor(r, 16, 24, 48, 255);
    SDL_RenderClear(r);
    SDL_Rect tdst = { 16, 16, 48, 48 };
    double tgt_angle = (double)((frame * 6) % 360);
    if (SDL_RenderCopyEx(r, tex, NULL, &tdst, tgt_angle, NULL,
                         SDL_FLIP_HORIZONTAL) != 0) {
        transform_errors++;
    }

    // Switch back to playfield
    if (SDL_SetRenderTarget(r, NULL) != 0) transform_errors++;
    SDL_SetRenderDrawColor(r, 20, 28, 40, 255);
    SDL_RenderClear(r);

    // 1. Identity sprite (1:1, 0 deg)
    SDL_Rect r_id = { 8, 8, 32, 32 };
    if (SDL_RenderCopy(r, tex, NULL, &r_id) != 0) transform_errors++;

    // 2. Rotating sprites at fixed key angles + animated
    static const double angles[7] = { 0.0, 30.0, 45.0, 90.0, 135.0, 180.0, 270.0 };
    for (int i = 0; i < 7; ++i) {
        SDL_Rect dst = { 48 + i * 38, 8, 32, 32 };
        if (SDL_RenderCopyEx(r, tex, NULL, &dst, angles[i], NULL,
                             SDL_FLIP_NONE) != 0) {
            transform_errors++;
        }
    }

    // 3. Scaled sprites (25%, 50%, 150%, 200%)
    SDL_Rect s25 = { 8, 52, 8, 8 };
    SDL_Rect s50 = { 22, 52, 16, 16 };
    SDL_Rect s150 = { 44, 48, 48, 48 };
    SDL_Rect s200 = { 98, 44, 64, 64 };
    if (SDL_RenderCopy(r, tex, NULL, &s25) != 0) transform_errors++;
    if (SDL_RenderCopy(r, tex, NULL, &s50) != 0) transform_errors++;
    if (SDL_RenderCopy(r, tex, NULL, &s150) != 0) transform_errors++;
    if (SDL_RenderCopy(r, tex, NULL, &s200) != 0) transform_errors++;

    // 4. Nonuniform scaling
    SDL_Rect nu_wide = { 170, 52, 64, 16 };
    SDL_Rect nu_tall = { 242, 48, 16, 64 };
    if (SDL_RenderCopy(r, tex, NULL, &nu_wide) != 0) transform_errors++;
    if (SDL_RenderCopy(r, tex, NULL, &nu_tall) != 0) transform_errors++;

    // 5 & 6. Horizontal & vertical flip
    SDL_Rect fh = { 8, 120, 32, 32 };
    SDL_Rect fv = { 48, 120, 32, 32 };
    SDL_Rect fhv = { 88, 120, 32, 32 };
    if (SDL_RenderCopyEx(r, tex, NULL, &fh, 0.0, NULL,
                         SDL_FLIP_HORIZONTAL) != 0) {
        transform_errors++;
    }
    if (SDL_RenderCopyEx(r, tex, NULL, &fv, 0.0, NULL,
                         SDL_FLIP_VERTICAL) != 0) {
        transform_errors++;
    }
    if (SDL_RenderCopyEx(r, tex, NULL, &fhv, 0.0, NULL,
                         (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL |
                                            SDL_FLIP_VERTICAL)) != 0) {
        transform_errors++;
    }

    // 7. Combined rotation + scaling + flip
    SDL_Rect rot_sc = { 132, 116, 48, 36 };
    if (SDL_RenderCopyEx(r, tex, NULL, &rot_sc,
                         (double)((frame * 9 + 30) % 360), NULL,
                         SDL_FLIP_HORIZONTAL) != 0) {
        transform_errors++;
    }

    // 8. Translucent sprite + alphaMod + colorMod
    SDL_SetTextureAlphaMod(alpha_tex, 160);
    SDL_SetTextureColorMod(alpha_tex, 255, 192, 192);
    SDL_Rect tr_dst = { 190, 120, 40, 40 };
    if (SDL_RenderCopyEx(r, alpha_tex, NULL, &tr_dst, 25.0, NULL,
                         SDL_FLIP_NONE) != 0) {
        transform_errors++;
    }
    SDL_SetTextureAlphaMod(alpha_tex, 255);
    SDL_SetTextureColorMod(alpha_tex, 255, 255, 255);

    // 9. Clipped at screen edge + explicit clip rect
    SDL_Rect clip_box = { 4, 168, 80, 64 };
    SDL_RenderSetClipRect(r, &clip_box);
    SDL_Rect edge_dst = { -12, 200, 48, 48 };
    if (SDL_RenderCopyEx(r, tex, NULL, &edge_dst, 45.0, NULL,
                         SDL_FLIP_NONE) != 0) {
        transform_errors++;
    }
    SDL_RenderSetClipRect(r, NULL);

    // 10b. Composite render target onto playfield + textured SDL_RenderGeometry
    SDL_Rect tgt_out = { 236, 152, 80, 80 };
    if (SDL_RenderCopyEx(r, target_tex, NULL, &tgt_out, 15.0, NULL,
                         SDL_FLIP_NONE) != 0) {
        transform_errors++;
    }

    SDL_Vertex gverts[4] = {
        { { 110.0f, 172.0f }, { 255, 255, 255, 255 }, { 0.02f, 0.02f } },
        { { 166.0f, 168.0f }, { 255, 255, 255, 255 }, { 0.98f, 0.02f } },
        { { 174.0f, 224.0f }, { 255, 255, 255, 255 }, { 0.98f, 0.98f } },
        { { 102.0f, 220.0f }, { 255, 255, 255, 255 }, { 0.02f, 0.98f } },
    };
    const int gidx[6] = { 0, 1, 2, 0, 2, 3 };
    if (SDL_RenderGeometry(r, tex, gverts, 4, gidx, 6) != 0) {
        transform_errors++;
    }

    // Edge cases: zero-size destination rect must succeed as a no-op
    SDL_Rect zero_dst = { 10, 10, 0, 16 };
    if (SDL_RenderCopyEx(r, tex, NULL, &zero_dst, 45.0, NULL,
                         SDL_FLIP_NONE) != 0) {
        transform_errors++;
    }

    SDL_RenderPresent(r);
    transform_frames++;
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu)
        ? 0x600D600Du : 0u;
    boot_marker = 0x56464C53u;

    VFLASH_ASSERT(vflash_clock_init());
#if defined(GPU_SDL2_MMU) && GPU_SDL2_MMU
    VFLASH_ASSERT(vflash_mmu_init() == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_identity() == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_enable() == VFLASH_MMU_OK);
    transform_cp15_c1 = vflash_mmu_control();
#else
    uint32_t c1 = 0;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(c1));
    transform_cp15_c1 = c1;
#endif

    VFLASH_ASSERT(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Window *win = SDL_CreateWindow("Transform Demo", 0, 0, 320, 240, 0);
    VFLASH_ASSERT(win != NULL);
    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    VFLASH_ASSERT(ren != NULL);

    SDL_RendererInfo info;
    VFLASH_ASSERT(SDL_GetRendererInfo(ren, &info) == 0);
    VFLASH_ASSERT((info.flags & SDL_RENDERER_ACCELERATED) != 0);
    VFLASH_ASSERT((info.flags & SDL_RENDERER_TARGETTEXTURE) != 0);

    build_asymmetric_texture(sprite_pixels, 255);
    build_asymmetric_texture(alpha_pixels, 140);

    SDL_Texture *tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 32, 32);
    VFLASH_ASSERT(tex != NULL);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    VFLASH_ASSERT(SDL_UpdateTexture(tex, NULL, sprite_pixels, 32 * 4) == 0);

    SDL_Texture *alpha_tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 32, 32);
    VFLASH_ASSERT(alpha_tex != NULL);
    SDL_SetTextureBlendMode(alpha_tex, SDL_BLENDMODE_BLEND);
    VFLASH_ASSERT(SDL_UpdateTexture(alpha_tex, NULL, alpha_pixels, 32 * 4) == 0);

    SDL_Texture *target_tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 80, 80);
    VFLASH_ASSERT(target_tex != NULL);
    SDL_SetTextureBlendMode(target_tex, SDL_BLENDMODE_BLEND);

    for (int f = 0; f < 16; ++f) {
        render_scene(ren, tex, alpha_tex, target_tex, f);
    }

    transform_status = (transform_errors == 0u) ? 0x600D600Du : 0xBAD00001u;

    for (int f = 16;; ++f) {
        render_scene(ren, tex, alpha_tex, target_tex, f);
    }
}
