/* Native alpha-blending diagnostic using shared 128x32 tiled backing
 * allocations: x=0..95 is the visible 96x32 playfield, x=96..127 is the
 * offscreen 32x32 sprite atlas.
 * Tests two blending paths:
 * 1. ARGB4444 textures using 0x86 bit 8 (0x8600013F).
 * 2. BGR555 textures using 0x88000002 and 0x8D808080 / 0x8D404040.
 * Exercises white sprites over Black, Red, Green, and Blue backgrounds:
 * - 0% alpha (transparent) -> background untouched
 * - 50% alpha white -> Black 0x3DEF, Red 0x3DFF, Green 0x3FEF, Blue 0x7DEF
 * - 25% alpha white -> Black 0x1CE7, Red 0x1CFF, Green 0x1FE7, Blue 0x7CE7
 * - 100% alpha white -> 0x7FFF
 * Tests both unclipped (tex_setup fast-path) and clipped (texel slow-path) draws. */
#include "vflash/gpu.h"
#include "vflash/video.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#ifdef GPU_ALPHA_MMU
#include "vflash/mmu.h"
#endif

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint16_t alpha_surface_a[32768] __attribute__((aligned(64)));
volatile uint16_t alpha_surface_b[32768] __attribute__((aligned(64)));
volatile uint16_t alpha_argb_asset[256] __attribute__((aligned(64)));
volatile uint16_t alpha_bgr_asset[256] __attribute__((aligned(64)));
volatile uint32_t gpu_alpha_status,gpu_alpha_frames,gpu_alpha_uploads,gpu_alpha_control;
volatile uint32_t gpu_operation_ticks,gpu_present_ticks,gpu_total_ticks;
volatile uint32_t gpu_submission_ticks,gpu_completion_ticks,gpu_lists;

static uint32_t cmd_list[64] __attribute__((aligned(64)));

static vflash_gpu_result_t submit_cmd(vflash_gpu_t *g, vflash_gpu_surface_t *s, uint32_t n)
{
    (void)g;
    uint32_t front = vflash_video_tiled_address();
    uint32_t fbytes = vflash_video_tiled_bytes();
    uint32_t sbytes = (s->fb.height >> 3) * 16384u;
    if (front && s->physical_address < front + fbytes && front < s->physical_address + sbytes)
        return VFLASH_GPU_BUSY;
    cmd_list[n] = 0x1400FD01;
    REG(0xA8000014) = 0x3F;
    REG(0xA8000090) = 512;
    REG(0xA8000098) = s->physical_address;
    REG(0xA8000028) = (uint32_t)(uintptr_t)cmd_list;
    __asm__ volatile("" ::: "memory");
    uint16_t stamp = vflash_clock_ticks();
    REG(0xA8000000) = 1;
    gpu_submission_ticks += (uint16_t)(vflash_clock_ticks() - stamp);
    uint16_t start = vflash_clock_ticks();
    while (!(REG(0xA8000014) & 8)) {
        if ((uint16_t)(vflash_clock_ticks() - start) >= 1000) return VFLASH_GPU_TIMEOUT;
    }
    gpu_completion_ticks += (uint16_t)(vflash_clock_ticks() - start);
    ++gpu_lists;
    REG(0xA8000014) = 0x3F;
    if (REG(0xA8000014) & 0x3F) return VFLASH_GPU_DEVICE_ERROR;
    return VFLASH_GPU_OK;
}

static vflash_gpu_result_t draw_sprite_raw(vflash_gpu_t *g, vflash_gpu_surface_t *s,
                                           int32_t sx, int32_t sy, int32_t w, int32_t h,
                                           int32_t dx, int32_t dy,
                                           uint32_t fmt, uint32_t blend_mode, uint32_t blend_factor)
{
    if (sx < 0 || sy < 0 || w < 0 || h < 0 ||
        (int64_t)sx + w > s->fb.width || (int64_t)sy + h > s->fb.height) return VFLASH_GPU_INVALID;
    int64_t l = dx, t = dy, r = (int64_t)dx + w, b = (int64_t)dy + h;
    if (l < s->clip_x) l = s->clip_x;
    if (t < s->clip_y) t = s->clip_y;
    if (r > (int64_t)s->clip_x + s->clip_w) r = (int64_t)s->clip_x + s->clip_w;
    if (b > (int64_t)s->clip_y + s->clip_h) b = (int64_t)s->clip_y + s->clip_h;
    if (l >= r || t >= b) return VFLASH_GPU_OK;
    int32_t u0 = (int32_t)(sx + (l - dx)), v0 = (int32_t)(sy + (t - dy));
    int32_t cw = (int32_t)(r - l), ch = (int32_t)(b - t);
    if (u0 < l + cw && l < u0 + cw && v0 < t + ch && t < v0 + ch) return VFLASH_GPU_UNSUPPORTED;
    uint32_t n = 0;
    cmd_list[n++] = 0x81000000;
    cmd_list[n++] = 0x82000000u | ((uint32_t)t << 12) | (uint32_t)l;
    cmd_list[n++] = 0x83000000u | ((uint32_t)(b - 1) << 12) | (uint32_t)(r - 1);
    cmd_list[n++] = 0x1E000000;
    cmd_list[n++] = 0x88000000u | (blend_mode & 0xFFFFFF);
    if (blend_mode) cmd_list[n++] = 0x8D000000u | (blend_factor & 0xFFFFFF);
    cmd_list[n++] = 0x86000000u | (fmt & 0xFFFFFF);
    cmd_list[n++] = 0x87000000;
    cmd_list[n++] = 0xC8010004;
    cmd_list[n++] = 0;
    cmd_list[n++] = ((uint32_t)t << 16) | (uint32_t)l;
    cmd_list[n++] = ((uint32_t)ch << 16) | (uint32_t)cw;
    cmd_list[n++] = ((uint32_t)v0 << 16) | (uint32_t)u0;
    if (blend_mode) {
        cmd_list[n++] = 0x88000000;
        cmd_list[n++] = 0x8DFFFFFF;
    }
    cmd_list[n++] = 0x95000000;
    return submit_cmd(g, s, n);
}

static void fill(vflash_gpu_t *g, vflash_gpu_surface_t *s, int x, int y, int w, int h, uint32_t c)
{
    VFLASH_ASSERT(vflash_gpu_fill_rect(g, s, x, y, w, h, c) == 0);
}

void vflash_main(void)
{
    startup_status = boot_marker == 0 && data_cookie == 0x13579BDF ? 0x600D600D : 0;
    boot_marker = 0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef GPU_ALPHA_MMU
    VFLASH_ASSERT(vflash_mmu_identity() == 0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000, 0xA8000000, 1) == 0);
    VFLASH_ASSERT(vflash_mmu_enable() == 0);
#endif
    uint32_t c;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(c));
    gpu_alpha_control = c;

    for (uint32_t i = 0; i < 32768; ++i) alpha_surface_a[i] = alpha_surface_b[i] = 0x255A;

    /* Populate ARGB4444 sprite (16x16):
     * Rows 0..3: Transparent (a=0): 0x0000
     * Rows 4..7: 50% White (a=7 -> al=8): 0x7FFF
     * Rows 8..11: 25% White (a=3 -> al=4): 0x3FFF
     * Rows 12..15: 100% Opaque White (a=15 -> al=16): 0xFFFF */
    for (uint32_t y = 0; y < 16; ++y) {
        uint16_t val;
        if (y < 4) val = 0x0000;
        else if (y < 8) val = 0x7FFF;
        else if (y < 12) val = 0x3FFF;
        else val = 0xFFFF;
        for (uint32_t x = 0; x < 16; ++x) alpha_argb_asset[y * 16 + x] = val;
    }

    /* Populate BGR555 sprite (16x16):
     * Rows 0..3: Transparent: 0x8000
     * Rows 4..15: White: 0x7FFF (drawn with varying blend factors: 50%, 25%, 100%) */
    for (uint32_t y = 0; y < 16; ++y) {
        uint16_t val = (y < 4) ? 0x8000 : 0x7FFF;
        for (uint32_t x = 0; x < 16; ++x) alpha_bgr_asset[y * 16 + x] = val;
    }

    vflash_framebuffer_t fb;
    VFLASH_ASSERT(vflash_video_init(&fb));
    video_status = 0x56494431;
    draw_clear(&fb, 0);
    VFLASH_ASSERT(vflash_video_present(&fb));

    vflash_gpu_t g;
    vflash_gpu_surface_t a, b, argb_surf, bgr_surf;
    vflash_framebuffer_t argb_fb = {(uint16_t *)alpha_argb_asset, 16, 16, 16, 256, VFLASH_BGR555};
    vflash_framebuffer_t bgr_fb = {(uint16_t *)alpha_bgr_asset, 16, 16, 16, 256, VFLASH_BGR555};

    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&a, (void *)alpha_surface_a, (uint32_t)(uintptr_t)alpha_surface_a, 128, 32, sizeof(alpha_surface_a)) == 0);
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&b, (void *)alpha_surface_b, (uint32_t)(uintptr_t)alpha_surface_b, 128, 32, sizeof(alpha_surface_b)) == 0);
    VFLASH_ASSERT(vflash_gpu_surface_register(&argb_surf, &argb_fb, (uint32_t)(uintptr_t)alpha_argb_asset, 1) == 0);
    VFLASH_ASSERT(vflash_gpu_surface_register(&bgr_surf, &bgr_fb, (uint32_t)(uintptr_t)alpha_bgr_asset, 1) == 0);
    VFLASH_ASSERT(vflash_gpu_init(&g, VFLASH_GPU_ZEVIO) == 0);

    /* Upload sprites into the offscreen atlas once per surface:
     * ARGB4444 sprite at (96, 8)
     * BGR555 sprite at (112, 8) */
    VFLASH_ASSERT(vflash_gpu_upload(&g, &a, &argb_surf, 0, 0, 16, 16, 96, 8) == 0);
    VFLASH_ASSERT(vflash_gpu_upload(&g, &a, &bgr_surf, 0, 0, 16, 16, 112, 8) == 0);
    VFLASH_ASSERT(vflash_gpu_upload(&g, &b, &argb_surf, 0, 0, 16, 16, 96, 8) == 0);
    VFLASH_ASSERT(vflash_gpu_upload(&g, &b, &bgr_surf, 0, 0, 16, 16, 112, 8) == 0);
    gpu_alpha_uploads = 4;

    uint16_t begin = vflash_clock_ticks();
    for (uint32_t frame = 0; frame < 32; ++frame) {
        vflash_gpu_surface_t *s = (frame & 1) ? &b : &a;
        uint16_t start = vflash_clock_ticks();

        /* Background: 4 quadrants on 96x32 visible playfield:
         * Top-left: Black (0..47, 0..15)
         * Top-right: Red (48..95, 0..15)
         * Bottom-left: Green (0..47, 16..31)
         * Bottom-right: Blue (48..95, 16..31) */
        fill(&g, s, 0, 0, 48, 16, 0x000000);
        fill(&g, s, 48, 0, 48, 16, 0xFF0000);
        fill(&g, s, 0, 16, 48, 16, 0x00FF00);
        fill(&g, s, 48, 16, 48, 16, 0x0000FF);

        /* 1. ARGB4444 unclipped draws (16x16, 0x8600013F):
         * (8, 0) over Black
         * (56, 0) over Red
         * (8, 16) over Green
         * (56, 16) over Blue */
        VFLASH_ASSERT(draw_sprite_raw(&g, s, 96, 8, 16, 16, 8, 0, 0x013F, 0, 0) == 0);
        VFLASH_ASSERT(draw_sprite_raw(&g, s, 96, 8, 16, 16, 56, 0, 0x013F, 0, 0) == 0);
        VFLASH_ASSERT(draw_sprite_raw(&g, s, 96, 8, 16, 16, 8, 16, 0x013F, 0, 0) == 0);
        VFLASH_ASSERT(draw_sprite_raw(&g, s, 96, 8, 16, 16, 56, 16, 0x013F, 0, 0) == 0);

        /* 2. BGR555 unclipped draws (0x8600003F):
         * At (28, 0) over Black, (76, 0) over Red, (28, 16) over Green, (76, 16) over Blue.
         * Rows 0..3: transparent (0x8000)
         * Rows 4..7: 50% white (k=128)
         * Rows 8..11: 25% white (k=64)
         * Rows 12..15: 100% white (blend off) */
        static const int32_t bgr_x[4] = {28, 76, 28, 76};
        static const int32_t bgr_y[4] = {0, 0, 16, 16};
        for (int q = 0; q < 4; ++q) {
            int32_t dx = bgr_x[q], dy = bgr_y[q];
            /* Rows 0..7: 0..3 transparent, 4..7 50% white */
            VFLASH_ASSERT(draw_sprite_raw(&g, s, 112, 8, 16, 8, dx, dy, 0x003F, 2, 0x808080) == 0);
            /* Rows 8..11: 25% white (k=64) */
            VFLASH_ASSERT(draw_sprite_raw(&g, s, 112, 16, 16, 4, dx, dy + 8, 0x003F, 2, 0x404040) == 0);
            /* Rows 12..15: 100% opaque white */
            VFLASH_ASSERT(draw_sprite_raw(&g, s, 112, 20, 16, 4, dx, dy + 12, 0x003F, 0, 0) == 0);
        }

        /* 3. Clipped draws (texel slow-path, area < 256):
         * Clipped ARGB4444 sprite at (-4, 2) clipped to (1, 2, 6, 12) inside Black padding.
         * Clipped BGR555 sprite at (-4, 18) clipped to (1, 18, 6, 12) inside Green padding. */
        VFLASH_ASSERT(vflash_gpu_clip(s, 1, 2, 6, 12) == 0);
        VFLASH_ASSERT(draw_sprite_raw(&g, s, 96, 8, 16, 16, -4, 2, 0x013F, 0, 0) == 0);

        VFLASH_ASSERT(vflash_gpu_clip(s, 1, 18, 6, 12) == 0);
        VFLASH_ASSERT(draw_sprite_raw(&g, s, 112, 8, 16, 8, -4, 18, 0x003F, 2, 0x808080) == 0);
        VFLASH_ASSERT(draw_sprite_raw(&g, s, 112, 16, 16, 4, -4, 26, 0x003F, 2, 0x404040) == 0);

        VFLASH_ASSERT(vflash_gpu_clip(s, 0, 0, 128, 32) == 0);
        VFLASH_ASSERT(vflash_gpu_wait_idle(&g) == 0);
        gpu_operation_ticks += (uint16_t)(vflash_clock_ticks() - start);

        start = vflash_clock_ticks();
        VFLASH_ASSERT(vflash_gpu_present_view(&g, s, 96, 32, 16, 24) == 0);
        gpu_present_ticks += (uint16_t)(vflash_clock_ticks() - start);

        /* Frontbuffer protection: reject draws to the displayed buffer */
        VFLASH_ASSERT(draw_sprite_raw(&g, s, 96, 8, 16, 16, 8, 0, 0x013F, 0, 0) == VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_fill_rect(&g, s, 0, 0, 1, 1, 0) == VFLASH_GPU_BUSY);
        gpu_alpha_frames = frame + 1;
    }
    gpu_total_ticks = (uint16_t)(vflash_clock_ticks() - begin);

    VFLASH_ASSERT(vflash_gpu_shutdown(&g) == 0);
    VFLASH_ASSERT(REG(0xB8000108) == 0 && REG(0xB8000160) == (uint32_t)(uintptr_t)fb.pixels);
    VFLASH_ASSERT(vflash_gpu_wait_idle(&g) == VFLASH_GPU_NOT_INITIALIZED);
    VFLASH_ASSERT(vflash_gpu_init(&g, VFLASH_GPU_ZEVIO) == 0);

#if GPU_ALPHA_FINAL == 0
    VFLASH_ASSERT(vflash_gpu_present_view(&g, &a, 96, 32, 0, 0) == 0);
#elif GPU_ALPHA_FINAL == 1
    VFLASH_ASSERT(vflash_gpu_present_view(&g, &b, 96, 32, 224, 208) == 0);
#else
    VFLASH_ASSERT(vflash_gpu_shutdown(&g) == 0);
#endif
    VFLASH_ASSERT(vflash_panic_vectors_intact());
    render_status = 0x44524157;
    gpu_alpha_status = 0x47414C31;
    for (;;) __asm__ volatile("nop");
}

