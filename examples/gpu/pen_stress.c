// Scratch-style pen layer stress test: 1200+ stamps, SDL_RenderGeometry trails,
// persistent target across frames, repeated clear/redraw, and MMU OFF vs ON.

#include <SDL.h>
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/mmu.h"

#include <stdint.h>

volatile uint32_t startup_status;
volatile uint32_t pen_stress_status;
volatile uint32_t pen_stress_frames;
volatile uint32_t pen_stress_stamps;
volatile uint32_t pen_stress_geom_tris;
volatile uint32_t pen_stress_clears;
volatile uint32_t pen_stress_cp15_c1;

static uint32_t boot_marker;
static uint32_t data_cookie = 0x13579BDFu;
static uint32_t stamp_pixels[16 * 16];

static void present_stage(SDL_Renderer *r, SDL_Texture *pen_tex, int frame_idx)
{
    SDL_SetRenderTarget(r, NULL);
    SDL_SetRenderDrawColor(r, 24, 32, 56, 255);
    SDL_RenderClear(r);

    // Moving background indicator to verify double-buffering
    SDL_SetRenderDrawColor(r, 40, 80, 120, 255);
    SDL_Rect bar = { (frame_idx * 4) % 288, 0, 32, 12 };
    SDL_RenderFillRect(r, &bar);

    SDL_Rect dst = { 40, 30, 240, 180 };
    SDL_RenderCopy(r, pen_tex, NULL, &dst);
    SDL_RenderPresent(r);
    pen_stress_frames++;
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0u;
    boot_marker = 0x56464C53u;

    VFLASH_ASSERT(vflash_clock_init());
#if defined(GPU_SDL2_MMU) && GPU_SDL2_MMU
    VFLASH_ASSERT(vflash_mmu_init() == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_identity() == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_enable() == VFLASH_MMU_OK);
    pen_stress_cp15_c1 = vflash_mmu_control();
#else
    uint32_t c1 = 0;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(c1));
    pen_stress_cp15_c1 = c1;
#endif

    VFLASH_ASSERT(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Window *win = SDL_CreateWindow("Pen Stress", 0, 0, 320, 240, 0);
    VFLASH_ASSERT(win != NULL);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    VFLASH_ASSERT(ren != NULL);

    // 16x16 translucent RGBA8888 stamp sprite (transparent border, colored diamond)
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            int dx = x - 8;
            int dy = y - 8;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            stamp_pixels[y * 16 + x] = (dx + dy <= 6) ? 0x00FF00FFu : 0x00000000u;
        }
    }
    SDL_Texture *stamp = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_STATIC, 16, 16);
    VFLASH_ASSERT(stamp != NULL);
    SDL_SetTextureBlendMode(stamp, SDL_BLENDMODE_BLEND);
    VFLASH_ASSERT(SDL_UpdateTexture(stamp, NULL, stamp_pixels, 16 * 4) == 0);

    SDL_Texture *pen_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET, 240, 180);
    VFLASH_ASSERT(pen_tex != NULL);
    SDL_SetTextureBlendMode(pen_tex, SDL_BLENDMODE_BLEND);

    int frame = 0;

    // Phase 1: 1,200 stamps in a single frame (> 256 VFLASH_MAX_TARGET_OPS)
    VFLASH_ASSERT(SDL_SetRenderTarget(ren, pen_tex) == 0);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);
    for (int i = 0; i < 1200; ++i) {
        SDL_Rect dst = { (i * 17) % 224, (i * 13) % 164, 16, 16 };
        SDL_RenderCopy(ren, stamp, NULL, &dst);
        pen_stress_stamps++;
    }
    // Present 6 frames without touching pen_tex to verify double-buffer persistence
    for (int k = 0; k < 6; ++k) {
        present_stage(ren, pen_tex, frame++);
    }

    // Phase 2: Persistent SDL_RenderGeometry + SDL_RenderDrawLine trails across 40 frames
    for (int f = 0; f < 40; ++f) {
        VFLASH_ASSERT(SDL_SetRenderTarget(ren, pen_tex) == 0);
        SDL_Vertex verts[6];
        SDL_Color col = { (Uint8)(64 + f * 4), (Uint8)(220 - f * 3), 255, 255 };
        for (int q = 0; q < 30; ++q) {
            float x0 = (float)((f * 11 + q * 7) % 220);
            float y0 = (float)((f * 9 + q * 5) % 160);
            float x1 = x0 + 12.0f;
            float y1 = y0 + 6.0f;
            verts[0] = (SDL_Vertex){ { x0, y0 }, col, { 0.0f, 0.0f } };
            verts[1] = (SDL_Vertex){ { x0, y1 }, col, { 0.0f, 0.0f } };
            verts[2] = (SDL_Vertex){ { x1, y0 }, col, { 0.0f, 0.0f } };
            verts[3] = (SDL_Vertex){ { x0, y1 }, col, { 0.0f, 0.0f } };
            verts[4] = (SDL_Vertex){ { x1, y1 }, col, { 0.0f, 0.0f } };
            verts[5] = (SDL_Vertex){ { x1, y0 }, col, { 0.0f, 0.0f } };
            VFLASH_ASSERT(SDL_RenderGeometry(ren, NULL, verts, 6, NULL, 0) == 0);
            pen_stress_geom_tris += 2;
        }
        SDL_SetRenderDrawColor(ren, 255, 220, 40, 255);
        SDL_RenderDrawLine(ren, 8, (f * 4) % 172, 232, 172 - ((f * 4) % 172));
        present_stage(ren, pen_tex, frame++);
    }

    // Phase 3: 10 repeated clear + redraw cycles
    for (int c = 0; c < 10; ++c) {
        VFLASH_ASSERT(SDL_SetRenderTarget(ren, pen_tex) == 0);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
        SDL_RenderClear(ren);
        pen_stress_clears++;

        for (int s = 0; s < 12; ++s) {
            SDL_Rect dst = { 10 + s * 18, 20 + ((c * 13 + s * 7) % 130), 16, 16 };
            SDL_RenderCopy(ren, stamp, NULL, &dst);
        }
        SDL_SetRenderDrawColor(ren, 255, 64, 64, 255);
        SDL_RenderDrawLine(ren, 0, c * 16, 239, 179 - c * 16);
        present_stage(ren, pen_tex, frame++);
        present_stage(ren, pen_tex, frame++);
    }

    pen_stress_status = 0x50454E53u; // "PENS"
    for (;;) {
        present_stage(ren, pen_tex, frame++);
    }
}
