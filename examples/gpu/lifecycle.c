/* Freestanding 2D renderer lifecycle and atlas management demonstration:
 * validates texture invalidation, atlas reset, allocation exhaustion,
 * frontbuffer upload protection, and dual-surface coherency across scene changes. */
#include "vflash/renderer.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#ifdef GPU_RENDERER_MMU
#include "vflash/mmu.h"
#endif

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker, startup_status, video_status, render_status;
volatile uint32_t data_cookie = 0x13579BDF;
volatile uint16_t lifecycle_surface_a[32768] __attribute__((aligned(64)));
volatile uint16_t lifecycle_surface_b[32768] __attribute__((aligned(64)));
volatile uint16_t lifecycle_s1_bgr[256] __attribute__((aligned(64)));
volatile uint16_t lifecycle_s1_abgr[256] __attribute__((aligned(64)));
volatile uint16_t lifecycle_s2_bgr_a[128] __attribute__((aligned(64)));
volatile uint16_t lifecycle_s2_abgr_b[128] __attribute__((aligned(64)));
volatile uint16_t lifecycle_s2_abgr_c[256] __attribute__((aligned(64)));

volatile uint32_t gpu_lifecycle_status, gpu_lifecycle_frames, gpu_lifecycle_control;
volatile uint32_t gpu_operation_ticks, gpu_present_ticks, gpu_total_ticks;
volatile uint32_t gpu_submission_ticks, gpu_completion_ticks, gpu_lists;
volatile uint32_t gpu_exhaustion_passed, gpu_busy_passed, gpu_invalidation_passed;

static uint16_t s1_bgr_pixel(uint32_t x, uint32_t y)
{
    if ((x < 3 && y < 3) || (x >= 13 && y < 3)) return (x == 0 && y == 0) ? 0x0000 : 0x8000;
    if ((x < 3 && y >= 13) || (x >= 13 && y >= 13)) return (x == 15 && y == 15) ? 0xFFFF : 0x9ABC;
    if (x >= 6 && x <= 9 && y >= 6 && y <= 9) return ((x + y) & 1) ? 0x8000 : 0x0000;
    if (x == 0 || x == 15 || y == 0 || y == 15) return 0x7FFF;
    if (x == y) return 0x03FF;
    if (x + y == 15) return 0x7FE0;
    uint32_t base = (y < 8) ? ((x < 8) ? 0x001Fu : 0x03E0u) : ((x < 8) ? 0x7C00u : 0x7C1Fu);
    return (uint16_t)((base ^ (((y & 7u) << 5) | (x & 7u))) & 0x7FFFu);
}

static uint16_t s1_abgr_pixel(uint32_t x, uint32_t y)
{
    if ((x < 2 && y < 2) || (x >= 14 && y < 2) || (x < 2 && y >= 14) || (x >= 14 && y >= 14))
        return 0x0FFF;
    if (x >= 6 && x <= 9 && y >= 6 && y <= 9) {
        if (y < 8) return (x < 8) ? 0xF000 : 0xFFFF;
        return (x < 8) ? 0x70FF : 0x3FF0;
    }
    if (y < 8) return (x < 8) ? 0x700F : 0x70F0;
    return (x < 8) ? 0x7F00 : 0xBF0F;
}

static uint16_t s2_bgr_a_pixel(uint32_t x, uint32_t y)
{
    if (x == 0 || x == 7 || y == 0 || y == 15) return 0x7FE0; /* yellow border */
    if (y >= 6 && y <= 9) return 0x8000; /* transparent key stripe */
    return (uint16_t)(0x001F | ((x & 7u) << 5) | ((y & 15u) << 10));
}

static uint16_t s2_abgr_b_pixel(uint32_t x, uint32_t y)
{
    if (x == 0 || x == 7 || y == 0 || y == 15) return 0x0FFF; /* 0-alpha border */
    return (uint16_t)(0x7000 | (x & 15u) | ((y & 15u) << 8)); /* 50% alpha gradient */
}

static uint16_t s2_abgr_c_pixel(uint32_t x, uint32_t y)
{
    if ((x < 3 && y < 3) || (x >= 13 && y < 3) || (x < 3 && y >= 13) || (x >= 13 && y >= 13))
        return 0xF000; /* opaque black */
    if (x >= 5 && x <= 10 && y >= 5 && y <= 10)
        return 0xFFFF; /* opaque white */
    return (uint16_t)(0x8000 | ((x & 15u) << 4) | (y & 15u)); /* 50% alpha */
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDF) ? 0x600D600D : 0;
    boot_marker = 0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef GPU_RENDERER_MMU
    VFLASH_ASSERT(vflash_mmu_identity() == 0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000, 0xA8000000, 1) == 0);
    VFLASH_ASSERT(vflash_mmu_enable() == 0);
#endif
    uint32_t c;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(c));
    gpu_lifecycle_control = c;

    for (uint32_t i = 0; i < 32768; ++i) lifecycle_surface_a[i] = lifecycle_surface_b[i] = 0x255A;

    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            lifecycle_s1_bgr[y * 16 + x] = s1_bgr_pixel(x, y);
            lifecycle_s1_abgr[y * 16 + x] = s1_abgr_pixel(x, y);
            lifecycle_s2_abgr_c[y * 16 + x] = s2_abgr_c_pixel(x, y);
        }
    }
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            lifecycle_s2_bgr_a[y * 8 + x] = s2_bgr_a_pixel(x, y);
            lifecycle_s2_abgr_b[y * 8 + x] = s2_abgr_b_pixel(x, y);
        }
    }
    video_status = 0x56494431;

    vflash_renderer_t r;
    vflash_renderer_config_t cfg = {
        .surface_a = (void *)lifecycle_surface_a,
        .surface_b = (void *)lifecycle_surface_b,
        .surface_bytes = sizeof(lifecycle_surface_a),
        .surface_width = 128,
        .surface_height = 32,
        .playfield_width = 96,
        .playfield_height = 32,
        .screen_x = 16,
        .screen_y = 24,
        .atlas_x = 96,
        .atlas_y = 8,
        .atlas_width = 32,
        .atlas_height = 16
    };
    VFLASH_ASSERT(vflash_renderer_init(&r, &cfg) == 0);

    /* ========================================================
     * PHASE 1: Scene 1 Assets & Rendering
     * ======================================================== */
    vflash_texture_t tex_s1_bgr_opaque, tex_s1_bgr_keyed, tex_s1_abgr;
    VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &tex_s1_bgr_opaque, (const void *)lifecycle_s1_bgr, 16, 16, VFLASH_TEXTURE_OPAQUE) == 0);
    tex_s1_bgr_keyed = tex_s1_bgr_opaque;
    tex_s1_bgr_keyed.type = VFLASH_TEXTURE_KEYED;

    VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &tex_s1_abgr, (const void *)lifecycle_s1_abgr, 16, 16, VFLASH_TEXTURE_TRANSLUCENT) == 0);

    /* Test 1: Allocation exhaustion check (atlas shelf 32x16 is now 100% full) */
    vflash_texture_t overflow_tex;
    VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &overflow_tex, (const void *)lifecycle_s1_bgr, 16, 16, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_OUT_OF_MEMORY);
    gpu_exhaustion_passed = 1;

    uint16_t begin = vflash_clock_ticks();
    for (uint32_t frame = 0; frame < 16; ++frame) {
        uint16_t start = vflash_clock_ticks();

        VFLASH_ASSERT(vflash_renderer_clear(&r, 0x102060) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 0, 16, 96, 16, 0x105020) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 0, 14, 96, 4, 0xC08020) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 30, 0, 4, 32, 0x404040) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 62, 0, 4, 32, 0x404040) == 0);

        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s1_bgr_opaque, 4, 8) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s1_bgr_keyed, 26, 8) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s1_abgr, 48, 8) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s1_bgr_keyed, 70, 8) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s1_abgr, 52, 12) == 0);

        VFLASH_ASSERT(vflash_renderer_set_clip(&r, 24, 6, 20, 16) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s1_abgr, 20, 4) == 0);
        VFLASH_ASSERT(vflash_renderer_reset_clip(&r) == 0);

        gpu_operation_ticks += (uint16_t)(vflash_clock_ticks() - start);
        start = vflash_clock_ticks();
        VFLASH_ASSERT(vflash_renderer_present(&r) == 0);
        gpu_present_ticks += (uint16_t)(vflash_clock_ticks() - start);

        /* Test 2: Upload rejected while a surface is actively displayed */
        VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &overflow_tex, (const void *)lifecycle_s1_bgr, 16, 16, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_BUSY);
        gpu_busy_passed = 1;

        gpu_lifecycle_frames = frame + 1;
    }

    /* ========================================================
     * PHASE 2: Texture Invalidation & Atlas Reset
     * ======================================================== */
    /* Invalidate Scene 1 BGR texture */
    VFLASH_ASSERT(vflash_renderer_destroy_texture(&r, &tex_s1_bgr_opaque) == 0);
    VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s1_bgr_opaque, 0, 0) == VFLASH_GPU_INVALID);

    /* Reset the atlas for Scene 2 (clears frontbuffer lock and advances generation) */
    VFLASH_ASSERT(vflash_renderer_reset_atlas(&r) == 0);

    /* Stale texture handle from pre-reset generation is now rejected */
    VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s1_abgr, 0, 0) == VFLASH_GPU_INVALID);
    gpu_invalidation_passed = 1;

    /* Upload Scene 2 textures with different geometry into the fresh atlas:
     * - Pattern A (8x16 BGR) at (96, 8)
     * - Pattern B (8x16 ABGR) at (104, 8)
     * - Asset C (16x16 ABGR) at (112, 8) */
    vflash_texture_t tex_s2_a_opaque, tex_s2_a_keyed, tex_s2_b, tex_s2_c;
    VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &tex_s2_a_opaque, (const void *)lifecycle_s2_bgr_a, 8, 16, VFLASH_TEXTURE_OPAQUE) == 0);
    tex_s2_a_keyed = tex_s2_a_opaque;
    tex_s2_a_keyed.type = VFLASH_TEXTURE_KEYED;

    VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &tex_s2_b, (const void *)lifecycle_s2_abgr_b, 8, 16, VFLASH_TEXTURE_TRANSLUCENT) == 0);
    VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &tex_s2_c, (const void *)lifecycle_s2_abgr_c, 16, 16, VFLASH_TEXTURE_TRANSLUCENT) == 0);

    /* Render 16 frames of Scene 2 (frames 16..31) */
    for (uint32_t frame = 16; frame < 32; ++frame) {
        uint16_t start = vflash_clock_ticks();

        VFLASH_ASSERT(vflash_renderer_clear(&r, 0x602010) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 0, 16, 96, 16, 0x203050) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 0, 12, 96, 4, 0x904010) == 0);

        /* Draw Scene 2 assets */
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s2_a_opaque, 8, 8) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s2_a_keyed, 24, 8) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s2_b, 44, 8) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s2_c, 64, 8) == 0);

        /* Clipped Scene 2 draw */
        VFLASH_ASSERT(vflash_renderer_set_clip(&r, 36, 4, 32, 20) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_s2_c, 32, 6) == 0);
        VFLASH_ASSERT(vflash_renderer_reset_clip(&r) == 0);

        gpu_operation_ticks += (uint16_t)(vflash_clock_ticks() - start);
        start = vflash_clock_ticks();
        VFLASH_ASSERT(vflash_renderer_present(&r) == 0);
        gpu_present_ticks += (uint16_t)(vflash_clock_ticks() - start);

        gpu_lifecycle_frames = frame + 1;
    }
    gpu_total_ticks = (uint16_t)(vflash_clock_ticks() - begin);

    vflash_gpu_stats_t stats;
    VFLASH_ASSERT(vflash_gpu_stats(&r.gpu, &stats) == 0);
    gpu_submission_ticks = stats.submission_ticks;
    gpu_completion_ticks = stats.completion_ticks;
    gpu_lists = stats.lists;

    VFLASH_ASSERT(vflash_renderer_shutdown(&r) == 0);
    VFLASH_ASSERT(REG(0xB8000108) == 0 && REG(0xB8000160) == (uint32_t)(uintptr_t)r.linear_fb.pixels);

#if GPU_RENDERER_FINAL == 0
    vflash_gpu_t final_gpu;
    VFLASH_ASSERT(vflash_gpu_init(&final_gpu, VFLASH_GPU_ZEVIO) == 0);
    VFLASH_ASSERT(vflash_gpu_present_view(&final_gpu, &r.surf_a, 96, 32, 0, 0) == 0);
#elif GPU_RENDERER_FINAL == 1
    vflash_gpu_t final_gpu;
    VFLASH_ASSERT(vflash_gpu_init(&final_gpu, VFLASH_GPU_ZEVIO) == 0);
    VFLASH_ASSERT(vflash_gpu_present_view(&final_gpu, &r.surf_b, 96, 32, 224, 208) == 0);
#endif
    VFLASH_ASSERT(vflash_panic_vectors_intact());
    render_status = 0x44524157;
    gpu_lifecycle_status = 0x4C494645; /* "LIFE" */
    for (;;) __asm__ volatile("nop");
}

