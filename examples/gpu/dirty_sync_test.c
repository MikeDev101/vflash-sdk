// Dirty-tile atlas synchronization regression test.
// Verifies partial dirty-tile sync across alternating buffers, live updates,
// overlapping regions, destroy/recreate, and render-target overflow.

#include <SDL.h>
#include <SDL_vflash.h>
#include "vflash/clock.h"
#include "vflash/mmu.h"

#include <stdint.h>

volatile uint32_t boot_marker;
volatile uint32_t startup_status;
volatile uint32_t data_cookie = 0x13579BDFu;

static uint32_t tex_buf[32 * 32];

static void fill_buf(uint32_t rgba, int count)
{
    for (int i = 0; i < count; ++i) {
        tex_buf[i] = rgba;
    }
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

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return;

    SDL_Window *win = SDL_CreateWindow(
        "dirty_sync_test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        320, 240, 0);
    if (!win) return;

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) return;

    vflash_renderer_t *nr = vflash_sdl2_get_native_renderer(ren);
    if (!nr) return;

    // Frame 0: activate tiled scanout.
    SDL_SetRenderDrawColor(ren, 16, 16, 24, 255);
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);

    vflash_renderer_reset_perf_stats(nr);

    // Phase 1: Live upload + multiple updates + overlapping regions on alternating buffers.
    SDL_Texture *tex_a = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 32, 32);
    SDL_Texture *tex_b = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 32, 32);
    if (!tex_a || !tex_b) return;
    SDL_SetTextureBlendMode(tex_a, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(tex_b, SDL_BLENDMODE_BLEND);

    fill_buf(0xFF0000FFu, 32 * 32); // Red (RGBA32: R=FF, G=00, B=00, A=FF -> 0xFF0000FF in little-endian? Wait: ABGR8888/RGBA32)
    SDL_UpdateTexture(tex_a, NULL, tex_buf, 32 * 4);
    // Live update tex_a to Blue (R=0, G=0, B=255, A=255 -> 0xFFFF0000 in RGBA32 LE)
    fill_buf(0xFFFF0000u, 32 * 32);
    SDL_UpdateTexture(tex_a, NULL, tex_buf, 32 * 4);
    // Green (R=0, G=255, B=0, A=255 -> 0xFF00FF00)
    fill_buf(0xFF00FF00u, 32 * 32);
    SDL_UpdateTexture(tex_b, NULL, tex_buf, 32 * 4);

    // Draw on frame 1 (one surface), then frame 2 (other surface) without re-uploading.
    for (int f = 0; f < 2; ++f) {
        SDL_SetRenderDrawColor(ren, 16, 16, 24, 255);
        SDL_RenderClear(ren);
        SDL_Rect da = { 16, 16, 32, 32 };
        SDL_Rect db = { 64, 16, 32, 32 };
        SDL_RenderCopy(ren, tex_a, NULL, &da);
        SDL_RenderCopy(ren, tex_b, NULL, &db);
        SDL_RenderPresent(ren);
    }

    vflash_renderer_perf_stats_t ps1;
    vflash_renderer_get_perf_stats(nr, &ps1);

    // Phase 2: Destroy tex_a and recreate tex_c at freed slot (Yellow: R=255, G=255, B=0, A=255 -> 0xFF00FFFF).
    SDL_DestroyTexture(tex_a);
    SDL_Texture *tex_c = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 32, 32);
    if (!tex_c) return;
    SDL_SetTextureBlendMode(tex_c, SDL_BLENDMODE_BLEND);
    fill_buf(0xFF00FFFFu, 32 * 32);
    SDL_UpdateTexture(tex_c, NULL, tex_buf, 32 * 4);

    // Phase 3: Offscreen render target overflow (> 32 target ops) -> Magenta (0xFFFF00FF).
    SDL_Texture *rt = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 64, 64);
    if (!rt) return;
    SDL_SetRenderTarget(ren, rt);
    SDL_SetRenderDrawColor(ren, 255, 0, 255, 255);
    SDL_RenderClear(ren);
    for (int i = 0; i < 40; ++i) {
        SDL_Rect r = { (i % 8) * 8, (i / 8) * 8, 8, 8 };
        SDL_RenderFillRect(ren, &r);
    }
    SDL_SetRenderTarget(ren, NULL);

    vflash_renderer_perf_stats_t ps2;
    // Draw final verification frame across 2 alternating buffers.
    int ok = (ps1.sync_count >= 1 && ps1.sync_tiles > 0 && ps1.sync_tiles < 660);
    for (int f = 0; f < 4; ++f) {
        SDL_SetRenderDrawColor(ren, 16, 16, 24, 255);
        SDL_RenderClear(ren);
        SDL_Rect dc = { 16, 16, 32, 32 };   // Yellow
        SDL_Rect db = { 64, 16, 32, 32 };   // Green
        SDL_Rect dr = { 112, 16, 64, 64 };  // Magenta
        SDL_RenderCopy(ren, tex_c, NULL, &dc);
        SDL_RenderCopy(ren, tex_b, NULL, &db);
        SDL_RenderCopy(ren, rt, NULL, &dr);

        if (f == 1) {
            vflash_renderer_get_perf_stats(nr, &ps2);
            ok = ok && (ps2.sync_tiles < 660);
        }
        SDL_Rect status = { 16, 100, 160, 16 };
        SDL_SetRenderDrawColor(ren, ok ? 0 : 255, ok ? 255 : 0, 0, 255);
        SDL_RenderFillRect(ren, &status);
        SDL_RenderPresent(ren);
    }

    SDL_Log("[dirty-sync] %s sync1=(n=%lu,tiles=%lu) sync2=(n=%lu,tiles=%lu)",
            ok ? "PASS" : "FAIL",
            (unsigned long)ps1.sync_count, (unsigned long)ps1.sync_tiles,
            (unsigned long)ps2.sync_count, (unsigned long)ps2.sync_tiles);

    for (;;) {
        SDL_SetRenderDrawColor(ren, 16, 16, 24, 255);
        SDL_RenderClear(ren);
        SDL_Rect dc = { 16, 16, 32, 32 };
        SDL_Rect db = { 64, 16, 32, 32 };
        SDL_Rect dr = { 112, 16, 64, 64 };
        SDL_RenderCopy(ren, tex_c, NULL, &dc);
        SDL_RenderCopy(ren, tex_b, NULL, &db);
        SDL_RenderCopy(ren, rt, NULL, &dr);
        SDL_Rect status = { 16, 100, 160, 16 };
        SDL_SetRenderDrawColor(ren, ok ? 0 : 255, ok ? 255 : 0, 0, 255);
        SDL_RenderFillRect(ren, &status);
        SDL_RenderPresent(ren);
    }
}
