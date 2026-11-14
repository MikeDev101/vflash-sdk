// Benchmark comparing hardware 3D-quad sprite transformations
// (SDL_RenderCopyEx) against CPU software affine transformation +
// SDL_UpdateTexture + SDL_RenderCopy across sprite counts, sizes, and MMU
// configurations.

#include <SDL.h>
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/mmu.h"

#include <stdint.h>

typedef struct {
    uint32_t size;
    uint32_t count;
    uint32_t hw_rot_ticks;
    uint32_t sw_rot_ticks;
    uint32_t hw_sc_ticks;
    uint32_t sw_sc_ticks;
    uint32_t hw_rs_ticks;
    uint32_t sw_rs_ticks;
    uint32_t hw_tr_ticks;
    uint32_t hw_tgt_ticks;
} bench_entry_t;

volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t startup_status;
volatile uint32_t bench_status;
volatile uint32_t bench_cp15_c1;
volatile uint32_t bench_entry_count;
volatile bench_entry_t bench_entries[16];

static uint32_t src_buf[128 * 128];
static uint32_t dst_buf[192 * 192];
static uint16_t dst_buf16[192 * 192];

static void fill_pattern(uint32_t *buf, int w, int h, uint8_t alpha)
{
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t r = (uint8_t)((x * 255) / (w > 1 ? w - 1 : 1));
            uint8_t g = (uint8_t)((y * 255) / (h > 1 ? h - 1 : 1));
            uint8_t b = (uint8_t)(((x ^ y) & 15) * 16);
            buf[y * w + x] = ((uint32_t)alpha << 24) |
                             ((uint32_t)r << 16) |
                             ((uint32_t)g << 8) |
                             (uint32_t)b;
        }
    }
}

// CPU software nearest-neighbor affine transform into dst_buf
// Uses fixed-point Q16.16 math (representative of fast ARM926EJ-S software path)
static void sw_transform_sprite(const uint32_t *src, int sw, int sh,
                                uint32_t *dst, int dw, int dh,
                                int32_t cos_q16, int32_t sin_q16,
                                int32_t inv_scale_q16)
{
    int32_t cx_d = dw >> 1;
    int32_t cy_d = dh >> 1;
    int32_t cx_s = sw >> 1;
    int32_t cy_s = sh >> 1;
    int32_t m00 = (int32_t)(((int64_t)cos_q16 * inv_scale_q16) >> 16);
    int32_t m01 = (int32_t)(((int64_t)sin_q16 * inv_scale_q16) >> 16);

    for (int y = 0; y < dh; ++y) {
        int32_t dy = y - cy_d;
        int32_t row_u = (cx_s << 16) + dy * m01;
        int32_t row_v = (cy_s << 16) + dy * m00;
        uint32_t *drow = &dst[y * dw];
        for (int x = 0; x < dw; ++x) {
            int32_t dx = x - cx_d;
            int sx = (row_u + dx * m00) >> 16;
            int sy = (row_v - dx * m01) >> 16;
            if ((unsigned)sx < (unsigned)sw && (unsigned)sy < (unsigned)sh) {
                drow[x] = src[sy * sw + sx];
            } else {
                drow[x] = 0u;
            }
        }
    }
}

static uint32_t run_hw_case(SDL_Renderer *ren, SDL_Texture *tex,
                            SDL_Texture *target, int count,
                            int dw, int dh, double angle, int use_target)
{
    uint16_t t0 = vflash_clock_ticks();
    if (use_target) {
        SDL_SetRenderTarget(ren, target);
    } else {
        SDL_SetRenderTarget(ren, NULL);
    }
    SDL_SetRenderDrawColor(ren, 16, 16, 24, 255);
    SDL_RenderClear(ren);
    for (int i = 0; i < count; ++i) {
        SDL_Rect dst = { (i * 19) % 200, (i * 13) % 120, dw, dh };
        SDL_RenderCopyEx(ren, tex, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
    }
    if (use_target) {
        SDL_SetRenderTarget(ren, NULL);
        SDL_Rect out = { 16, 16, 16, 16 };
        SDL_RenderCopy(ren, target, NULL, &out);
    }
    uint16_t t1 = vflash_clock_ticks();
    SDL_RenderPresent(ren);
    return (uint32_t)(uint16_t)(t1 - t0);
}

static uint32_t run_sw_case(SDL_Renderer *ren, SDL_Texture *sw_tex,
                            int sw, int sh, int count, int dw, int dh,
                            int32_t cos_q16, int32_t sin_q16,
                            int32_t inv_scale_q16)
{
    uint16_t t0 = vflash_clock_ticks();
    SDL_SetRenderTarget(ren, NULL);
    SDL_SetRenderDrawColor(ren, 16, 16, 24, 255);
    SDL_RenderClear(ren);
    for (int i = 0; i < count; ++i) {
        sw_transform_sprite(src_buf, sw, sh, dst_buf, dw, dh,
                            cos_q16, sin_q16, inv_scale_q16);
        int pixels = dw * dh;
        for (int p = 0; p < pixels; ++p) {
            uint32_t v = dst_buf[p];
            uint16_t a4 = (uint16_t)((v >> 28) & 0xFu);
            uint16_t r4 = (uint16_t)((v >> 20) & 0xFu);
            uint16_t g4 = (uint16_t)((v >> 12) & 0xFu);
            uint16_t b4 = (uint16_t)((v >> 4) & 0xFu);
            dst_buf16[p] = (uint16_t)((a4 << 12) | (b4 << 8) | (g4 << 4) | r4);
        }
        SDL_Rect dst = { (i * 19) % 200, (i * 13) % 120, dw, dh };
        SDL_RenderCopy(ren, sw_tex, NULL, &dst);
    }
    uint16_t t1 = vflash_clock_ticks();
    SDL_RenderPresent(ren);
    return (uint32_t)(uint16_t)(t1 - t0);
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
    bench_cp15_c1 = vflash_mmu_control();
#else
    uint32_t c1 = 0;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(c1));
    bench_cp15_c1 = c1;
#endif

    VFLASH_ASSERT(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Window *win = SDL_CreateWindow("Transform Bench", 0, 0, 320, 240, 0);
    VFLASH_ASSERT(win != NULL);
    static const int sizes[4] = { 16, 32, 64, 128 };
    static const int counts[4] = { 1, 10, 25, 50 };
    static const int sc_sizes[4] = { 24, 48, 96, 96 }; // 1.5x for 16..64, 0.75x for 128

    // cos(37 deg) ~ 0.7986 (52339), sin(37 deg) ~ 0.6018 (39441)
    const int32_t cos37 = 52339;
    const int32_t sin37 = 39441;
    uint32_t entry_idx = 0;

    for (int si = 0; si < 4; ++si) {
        SDL_Renderer *ren = SDL_CreateRenderer(
            win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
        VFLASH_ASSERT(ren != NULL);

        int sz = sizes[si];
        int scaled = sc_sizes[si];
        int32_t inv_sc = (si == 3) ? 87381 : 43691; // 1/0.75 or 1/1.5

        SDL_Texture *hw_tex = SDL_CreateTexture(
            ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, sz, sz);
        SDL_Texture *sw_rot_tex = SDL_CreateTexture(
            ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, sz, sz);
        SDL_Texture *sw_sc_tex = SDL_CreateTexture(
            ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
            scaled, scaled);
        SDL_Texture *target = SDL_CreateTexture(
            ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 16, 16);
        VFLASH_ASSERT(hw_tex != NULL);
        VFLASH_ASSERT(sw_rot_tex != NULL);
        VFLASH_ASSERT(sw_sc_tex != NULL);
        VFLASH_ASSERT(target != NULL);

        SDL_SetTextureBlendMode(hw_tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureBlendMode(sw_rot_tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureBlendMode(sw_sc_tex, SDL_BLENDMODE_BLEND);
        fill_pattern(src_buf, sz, sz, 255);
        VFLASH_ASSERT(SDL_UpdateTexture(hw_tex, NULL, src_buf, sz * 4) == 0);
        VFLASH_ASSERT(SDL_UpdateTexture(sw_rot_tex, NULL, src_buf, sz * 4) == 0);
        VFLASH_ASSERT(SDL_UpdateTexture(sw_sc_tex, NULL, dst_buf, scaled * 4) == 0);

        for (int ci = 0; ci < 4; ++ci) {
            int cnt = counts[ci];
            uint32_t hw_rot = run_hw_case(ren, hw_tex, target, cnt,
                                          sz, sz, 37.0, 0);
            uint32_t sw_rot = run_sw_case(ren, sw_rot_tex, sz, sz, cnt,
                                          sz, sz, cos37, sin37, 65536);

            uint32_t hw_sc = run_hw_case(ren, hw_tex, target, cnt,
                                         scaled, scaled, 0.0, 0);
            uint32_t sw_sc = run_sw_case(ren, sw_sc_tex, sz, sz, cnt,
                                         scaled, scaled, 65536, 0, inv_sc);

            uint32_t hw_rs = run_hw_case(ren, hw_tex, target, cnt,
                                         scaled, scaled, 37.0, 0);
            uint32_t sw_rs = run_sw_case(ren, sw_sc_tex, sz, sz, cnt,
                                         scaled, scaled, cos37, sin37, inv_sc);

            SDL_SetTextureAlphaMod(hw_tex, 160);
            uint32_t hw_tr = run_hw_case(ren, hw_tex, target, cnt,
                                         sz, sz, 37.0, 0);
            SDL_SetTextureAlphaMod(hw_tex, 255);

            uint32_t hw_tgt = run_hw_case(ren, hw_tex, target, cnt,
                                          sz, sz, 37.0, 1);

            bench_entries[entry_idx].size = (uint32_t)sz;
            bench_entries[entry_idx].count = (uint32_t)cnt;
            bench_entries[entry_idx].hw_rot_ticks = hw_rot;
            bench_entries[entry_idx].sw_rot_ticks = sw_rot;
            bench_entries[entry_idx].hw_sc_ticks = hw_sc;
            bench_entries[entry_idx].sw_sc_ticks = sw_sc;
            bench_entries[entry_idx].hw_rs_ticks = hw_rs;
            bench_entries[entry_idx].sw_rs_ticks = sw_rs;
            bench_entries[entry_idx].hw_tr_ticks = hw_tr;
            bench_entries[entry_idx].hw_tgt_ticks = hw_tgt;
            entry_idx++;
            bench_entry_count = entry_idx;
        }

        SDL_DestroyTexture(target);
        SDL_DestroyTexture(sw_sc_tex);
        SDL_DestroyTexture(sw_rot_tex);
        SDL_DestroyTexture(hw_tex);
        SDL_DestroyRenderer(ren);
    }

    bench_status = 0x600D600Du;
    for (;;) {
    }
}
