// GPU texture atlas fragmentation & reuse verification test.
// Exercises realistic Scratch-like texture churn, adversarial checkerboard
// fragmentation, free-rect coalescing, and pixel-accurate reuse in FlashEm.

#include <SDL.h>
#include "SDL_vflash.h"
#include "vflash/clock.h"
#include "vflash/mmu.h"

#include <stdint.h>
#include <string.h>

volatile uint32_t boot_marker;
volatile uint32_t startup_status;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t frag_metrics[9];

static uint32_t scratch_pixels[320 * 240];

static void fill_abgr(uint32_t *buf, uint32_t count, uint32_t abgr)
{
    for (uint32_t i = 0; i < count; ++i) {
        buf[i] = abgr;
    }
}

static SDL_Texture *make_tex(SDL_Renderer *ren, int w, int h, uint32_t abgr)
{
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return NULL;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    fill_abgr(scratch_pixels, (uint32_t)w * (uint32_t)h, abgr);
    if (SDL_UpdateTexture(t, NULL, scratch_pixels, w * 4) < 0) {
        SDL_DestroyTexture(t);
        return NULL;
    }
    return t;
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
    SDL_Window *win = SDL_CreateWindow("atlas_frag_test", 0, 0, 320, 240, 0);
    if (!win) return;
    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) return;

    vflash_renderer_t *nr = vflash_sdl2_get_native_renderer(ren);
    if (!nr) return;

    vflash_atlas_stats_t st;
    int all_ok = (startup_status == 0x600D600Du) ? 1 : 0;

    if (vflash_renderer_get_atlas_stats(nr, &st) != VFLASH_GPU_OK ||
        st.total_texels != 704u * 240u || st.free_texels != st.total_texels ||
        st.free_rect_count != 1u || st.live_count != 0u) {
        all_ok = 0;
    }

    // Phase 1: Realistic Scratch workload churn (persistent font + UI +
    // swapping backdrops, costumes, and speech bubbles).
    SDL_Texture *font_tex = make_tex(ren, 64, 64, 0xFFFFFFFFu);
    SDL_Texture *ui_tex = make_tex(ren, 20, 10, 0xFFCCCCCCu);
    if (!font_tex || !ui_tex) all_ok = 0;

    uint32_t phase1_fails = 0;
    uint32_t phase1_min_largest = st.total_texels;
    uint32_t phase1_max_rects = 0;

    SDL_Texture *backdrop = NULL;
    SDL_Texture *costumes[3] = { NULL, NULL, NULL };
    SDL_Texture *bubbles[2] = { NULL, NULL };

    for (int iter = 0; iter < 16; ++iter) {
        int bw = (iter & 1) ? 220 : 240;
        int bh = (iter & 1) ? 165 : 180;
        if (backdrop) SDL_DestroyTexture(backdrop);
        backdrop = make_tex(ren, bw, bh, 0xFFE0E0E0u);
        if (!backdrop) phase1_fails++;

        for (int c = 0; c < 3; ++c) {
            if (costumes[c] && ((iter + c) & 1)) {
                SDL_DestroyTexture(costumes[c]);
                costumes[c] = NULL;
            }
            if (!costumes[c]) {
                int cw = (28 + ((iter * 7 + c * 13) % 36)) & ~1;
                int ch = 24 + ((iter * 5 + c * 11) % 32);
                costumes[c] = make_tex(ren, cw, ch, 0xFF4080FFu);
                if (!costumes[c]) phase1_fails++;
            }
        }

        for (int b = 0; b < 2; ++b) {
            if (bubbles[b]) SDL_DestroyTexture(bubbles[b]);
            int sw = (60 + ((iter * 9 + b * 17) % 40)) & ~1;
            int sh = 18 + ((iter + b) % 8);
            bubbles[b] = make_tex(ren, sw, sh, 0xFFFFFFFFu);
            if (!bubbles[b]) phase1_fails++;
        }

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (backdrop) {
            SDL_Rect bd = { 0, 0, 320, 240 };
            SDL_RenderCopy(ren, backdrop, NULL, &bd);
        }
        if (font_tex) {
            SDL_Rect fd = { 8, 8, 32, 32 };
            SDL_RenderCopy(ren, font_tex, NULL, &fd);
        }
        SDL_RenderPresent(ren);

        if (vflash_renderer_get_atlas_stats(nr, &st) == VFLASH_GPU_OK) {
            if (st.largest_free_texels < phase1_min_largest) {
                phase1_min_largest = st.largest_free_texels;
            }
            if (st.free_rect_count > phase1_max_rects) {
                phase1_max_rects = st.free_rect_count;
            }
        }
    }

    vflash_atlas_stats_t p1_end_st;
    vflash_renderer_get_atlas_stats(nr, &p1_end_st);

    if (backdrop) SDL_DestroyTexture(backdrop);
    for (int c = 0; c < 3; ++c) {
        if (costumes[c]) SDL_DestroyTexture(costumes[c]);
    }
    for (int b = 0; b < 2; ++b) {
        if (bubbles[b]) SDL_DestroyTexture(bubbles[b]);
    }
    SDL_DestroyTexture(font_tex);
    SDL_DestroyTexture(ui_tex);

    if (vflash_renderer_get_atlas_stats(nr, &st) != VFLASH_GPU_OK ||
        st.free_texels != st.total_texels || st.free_rect_count != 1u) {
        all_ok = 0;
    }
    if (p1_end_st.fail_meta != 0) all_ok = 0;

    // Phase 2: Adversarial checkerboard fragmentation (11x5 grid of 64x48 = 704x240).
    SDL_Texture *grid[55];
    memset(grid, 0, sizeof(grid));
    for (int i = 0; i < 55; ++i) {
        grid[i] = make_tex(ren, 64, 48, 0xFF808080u);
        if (!grid[i]) all_ok = 0;
    }
    // Free checkerboard slots (27 slots = 82,944 free texels, max hole 64x48).
    for (int i = 1; i < 55; i += 2) {
        SDL_DestroyTexture(grid[i]);
        grid[i] = NULL;
    }

    vflash_atlas_stats_t frag_st;
    vflash_renderer_get_atlas_stats(nr, &frag_st);
    uint32_t frag_before = frag_st.fail_frag;

    // Oversized allocation relative to max hole (96x64 = 6,144 < 36,864 free).
    SDL_Texture *oversize = make_tex(ren, 96, 64, 0xFFFFFFFFu);
    if (oversize != NULL) {
        all_ok = 0;
        SDL_DestroyTexture(oversize);
    }

    vflash_renderer_get_atlas_stats(nr, &frag_st);
    uint32_t phase2_frag_fails = frag_st.fail_frag - frag_before;
    if (phase2_frag_fails == 0) all_ok = 0;

    // Same-sized allocation (64x48) must still fit into a freed hole.
    SDL_Texture *fit_hole = make_tex(ren, 64, 48, 0xFF00FF00u);
    if (!fit_hole) {
        all_ok = 0;
    } else {
        SDL_DestroyTexture(fit_hole);
    }

    for (int i = 0; i < 55; ++i) {
        if (grid[i]) SDL_DestroyTexture(grid[i]);
    }

    vflash_atlas_stats_t post_st;
    vflash_renderer_get_atlas_stats(nr, &post_st);
    int coalesced_ok = (post_st.free_texels == post_st.total_texels &&
                        post_st.largest_free_texels == post_st.total_texels &&
                        post_st.free_rect_count == 1u &&
                        post_st.live_count == 0u);
    if (!coalesced_ok) all_ok = 0;

    // Phase 3: Reuse correctness — 7 distinct solid textures in reused slots.
    static const uint32_t colors[7] = {
        0xFF0000FFu, // Red   (ABGR)
        0xFF00FF00u, // Green
        0xFFFF0000u, // Blue
        0xFF00FFFFu, // Yellow
        0xFFFFFF00u, // Cyan
        0xFFFF00FFu, // Magenta
        0xFFFFFFFFu, // White
    };
    SDL_Texture *reuse_tex[7];
    for (int i = 0; i < 7; ++i) {
        reuse_tex[i] = make_tex(ren, 24, 24, colors[i]);
        if (!reuse_tex[i]) all_ok = 0;
    }

    // Phase 4: Expanded 704x240 atlas boundary, tile-crossing, and live-update checks.
    // Free Phase 3 textures first so we can lay out exact boundary allocations.
    for (int i = 0; i < 7; ++i) {
        if (reuse_tex[i]) SDL_DestroyTexture(reuse_tex[i]);
    }
    if (vflash_renderer_get_atlas_stats(nr, &st) != VFLASH_GPU_OK ||
        st.free_texels != st.total_texels || st.free_rect_count != 1u) {
        all_ok = 0;
    }

    // Filler 1: 304x240 at atlas x=320..623
    SDL_Texture *filler1 = make_tex(ren, 304, 240, 0xFF202020u);
    // Crosses X=639/640 (placed at x=624..655, y=0..31)
    SDL_Texture *tex_cross640 = make_tex(ren, 32, 32, 0xFF0000FFu); // Red (ABGR)
    // Filler 2: 288x240 at atlas x=656..943
    SDL_Texture *filler2 = make_tex(ren, 288, 240, 0xFF202020u);
    // Crosses X=959/960 (placed at x=944..975, y=0..31)
    SDL_Texture *tex_cross960 = make_tex(ren, 32, 32, 0xFFFFFFFFu); // White (for color mod)
    // Reaches X=1023 (placed at x=976..1023, y=0..31, crosses X=991/992)
    SDL_Texture *tex_right_edge = make_tex(ren, 48, 32, 0x80FFFFFFu); // ~50% alpha white
    // Crosses Y=231/232 and reaches Y=239 (placed at x=624..655, y=32..239)
    SDL_Texture *tex_bottom = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                                SDL_TEXTUREACCESS_STATIC, 32, 208);
    if (!filler1 || !tex_cross640 || !filler2 || !tex_cross960 || !tex_right_edge || !tex_bottom) {
        all_ok = 0;
    } else {
        SDL_SetTextureBlendMode(tex_bottom, SDL_BLENDMODE_BLEND);
        fill_abgr(scratch_pixels, 32u * 200u, 0xFF00FF00u); // Green rows 0..199 (atlas y=32..231)
        fill_abgr(scratch_pixels + 32u * 200u, 32u * 8u, 0xFFFF00FFu); // Magenta rows 200..207 (atlas y=232..239)
        if (SDL_UpdateTexture(tex_bottom, NULL, scratch_pixels, 32 * 4) < 0) all_ok = 0;
    }

    // Re-allocate the 7 Phase 3 swatches in the remaining space (944..1023, y=32..239).
    for (int i = 0; i < 7; ++i) {
        reuse_tex[i] = make_tex(ren, 24, 24, colors[i]);
        if (!reuse_tex[i]) all_ok = 0;
    }

    // Present one frame so tiled scanout is active before live updates.
    SDL_SetRenderDrawColor(ren, 16, 16, 24, 255);
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);

    // Final frames: test intra-frame live updates, ordering, color mod, alpha, and destroy/recreate.
    for (int frame = 0; frame < 4; ++frame) {
        SDL_SetRenderDrawColor(ren, 16, 16, 24, 255);
        SDL_RenderClear(ren);

        for (int i = 0; i < 7; ++i) {
            if (reuse_tex[i]) {
                SDL_Rect dst = { 16 + i * 36, 24, 24, 24 };
                SDL_RenderCopy(ren, reuse_tex[i], NULL, &dst);
            }
        }

        // Reset tex_cross640 to Red, draw at x=60; update to Cyan, draw at x=92; update to Yellow, draw at x=124.
        if (tex_cross640) {
            fill_abgr(scratch_pixels, 32u * 32u, 0xFF0000FFu); // Red
            if (SDL_UpdateTexture(tex_cross640, NULL, scratch_pixels, 32 * 4) < 0) all_ok = 0;
            SDL_Rect d1 = { 60, 120, 24, 24 };
            SDL_RenderCopy(ren, tex_cross640, NULL, &d1);

            fill_abgr(scratch_pixels, 32u * 32u, 0xFFFFFF00u); // Cyan
            if (SDL_UpdateTexture(tex_cross640, NULL, scratch_pixels, 32 * 4) < 0) all_ok = 0;
            SDL_Rect d2 = { 92, 120, 24, 24 };
            SDL_RenderCopy(ren, tex_cross640, NULL, &d2);

            fill_abgr(scratch_pixels, 32u * 32u, 0xFF00FFFFu); // Yellow
            if (SDL_UpdateTexture(tex_cross640, NULL, scratch_pixels, 32 * 4) < 0) all_ok = 0;
            SDL_Rect d3 = { 124, 120, 24, 24 };
            SDL_RenderCopy(ren, tex_cross640, NULL, &d3);
        }

        // Color-modulated texture crossing X=959/960
        if (tex_cross960) {
            SDL_SetTextureColorMod(tex_cross960, 0, 255, 128);
            SDL_Rect d4 = { 156, 120, 24, 24 };
            SDL_RenderCopy(ren, tex_cross960, NULL, &d4);
        }

        // Alpha-blended texture reaching X=1023
        if (tex_right_edge) {
            SDL_Rect d5 = { 188, 120, 24, 24 };
            SDL_RenderCopy(ren, tex_right_edge, NULL, &d5);
        }

        // Bottom-row texture crossing Y=231/232 down to Y=239
        if (tex_bottom) {
            SDL_Rect s6 = { 0, 176, 32, 32 };
            SDL_Rect d6 = { 16, 120, 32, 32 };
            SDL_RenderCopy(ren, tex_bottom, &s6, &d6);
        }

        // Destroy and recreate a texture while atlas_sync_pending is active
        if (filler1) {
            SDL_DestroyTexture(filler1);
            filler1 = make_tex(ren, 64, 64, 0xFFFF8000u); // Blue-ish (ABGR)
            if (!filler1) all_ok = 0;
        }
        if (filler1) {
            SDL_Rect s7 = { 0, 0, 24, 24 };
            SDL_Rect d7 = { 220, 120, 24, 24 };
            SDL_RenderCopy(ren, filler1, &s7, &d7);
        }

        // Status bar: green if all checks passed, red otherwise
        if (all_ok) {
            SDL_SetRenderDrawColor(ren, 0, 224, 0, 255);
        } else {
            SDL_SetRenderDrawColor(ren, 224, 0, 0, 255);
        }
        SDL_Rect status_bar = { 16, 80, 240, 24 };
        SDL_RenderFillRect(ren, &status_bar);

        SDL_RenderPresent(ren);
    }

    // Final cleanup & coalescing verification
    if (filler1) SDL_DestroyTexture(filler1);
    if (filler2) SDL_DestroyTexture(filler2);
    if (tex_cross640) SDL_DestroyTexture(tex_cross640);
    if (tex_cross960) SDL_DestroyTexture(tex_cross960);
    if (tex_right_edge) SDL_DestroyTexture(tex_right_edge);
    if (tex_bottom) SDL_DestroyTexture(tex_bottom);
    for (int i = 0; i < 7; ++i) {
        if (reuse_tex[i]) SDL_DestroyTexture(reuse_tex[i]);
    }
    vflash_atlas_stats_t final_st;
    if (vflash_renderer_get_atlas_stats(nr, &final_st) != VFLASH_GPU_OK ||
        final_st.free_texels != final_st.total_texels ||
        final_st.largest_free_texels != final_st.total_texels ||
        final_st.free_rect_count != 1u || final_st.live_count != 0u) {
        all_ok = 0;
    }

    frag_metrics[0] = phase1_fails;
    frag_metrics[1] = phase1_min_largest;
    frag_metrics[2] = phase1_max_rects;
    frag_metrics[3] = frag_st.free_texels;
    frag_metrics[4] = frag_st.largest_free_texels;
    frag_metrics[5] = phase2_frag_fails;
    frag_metrics[6] = final_st.fail_frag;
    frag_metrics[7] = final_st.fail_oom;
    frag_metrics[8] = final_st.fail_meta;
}
