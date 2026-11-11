/* Native Zevio 3D textured triangle diagnostic.
 * Validates opcode 0x44 with textured vertex format (vtx_fmt 0x10),
 * texture atlas binding (opcode 0x86 size/format, opcode 0x87 origin),
 * affine and perspective-correct interpolation, coordinate wrapping (UV > 1.0),
 * scissor clipping, and 320x240 double-buffered presentation. */
#include "vflash/gpu.h"
#include "vflash/video.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#ifdef GPU_TRIANGLE_MMU
#include "vflash/mmu.h"
#endif

#ifndef GPU_TRIANGLE_FINAL
#define GPU_TRIANGLE_FINAL 0
#endif

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

volatile uint32_t boot_marker, startup_status, video_status, render_status;
volatile uint32_t data_cookie = 0x13579BDF;

/* 320x272 tiled backing surfaces:
 * 30 tile rows of visible 320x240 playfield + 4 tile rows (32 scanlines) of offscreen texture atlas.
 * (272/8) * 16384 / 2 = 278,528 uint16_t (544 KiB / 557,056 bytes) each. */
volatile uint16_t triangle_surface_a[278528] __attribute__((aligned(64)));
volatile uint16_t triangle_surface_b[278528] __attribute__((aligned(64)));

volatile uint32_t gpu_triangle_status, gpu_triangle_frames, gpu_triangle_control;
volatile uint32_t gpu_operation_ticks, gpu_present_ticks, gpu_total_ticks;
volatile uint32_t gpu_submission_ticks, gpu_completion_ticks, gpu_lists;

static inline uint16_t f16_from_int(int val)
{
    if (val <= 0) return 0;
    uint32_t v = (uint32_t)val;
    int highest = 31 - __builtin_clz(v);
    int exp = highest + 31;
    uint32_t mant;
    if (highest >= 9) {
        mant = (v >> (highest - 9)) & 0x1FF;
    } else {
        mant = (v << (9 - highest)) & 0x1FF;
    }
    return (uint16_t)(((uint32_t)exp << 9) | mant);
}

#define F16_0_0 0x0000u
#define F16_0_5 0x3C00u
#define F16_1_0 0x3E00u
#define F16_2_0 0x4000u

static uint32_t cmd_buffer[128] __attribute__((aligned(64)));

static void submit_cmd(uint32_t phys_addr, uint32_t n)
{
    cmd_buffer[n] = 0x1400FD01; /* terminate list */
    REG(0xA8000014) = 0x3F;
    REG(0xA8000090) = 512;
    REG(0xA8000098) = phys_addr;
    REG(0xA8000028) = (uint32_t)(uintptr_t)cmd_buffer;
    __asm__ volatile("" ::: "memory");
    uint16_t s_sub = vflash_clock_ticks();
    REG(0xA8000000) = 1;
    gpu_submission_ticks += (uint16_t)(vflash_clock_ticks() - s_sub);

    uint16_t s_wait = vflash_clock_ticks();
    while (!(REG(0xA8000014) & 8)) {
        VFLASH_ASSERT((uint16_t)(vflash_clock_ticks() - s_wait) < 1000);
    }
    gpu_completion_ticks += (uint16_t)(vflash_clock_ticks() - s_wait);
    ++gpu_lists;
    REG(0xA8000014) = 0x3F;
}

static inline void emit_3d_pipeline_setup(uint32_t *buf, uint32_t *p_idx)
{
    uint32_t idx = *p_idx;
    /* Scale X, Y, Z = 1.0f (f16 0x3E00) */
    buf[idx++] = 0x203C0003;
    buf[idx++] = (0x3E00u << 16) | 0x3E00u;
    buf[idx++] = 0x3E00u;

    /* Matrix 1 = 3x3 Identity */
    buf[idx++] = 0x20400009;
    buf[idx++] = (0x0000u << 16) | 0x3E00u;
    buf[idx++] = (0x0000u << 16) | 0x0000u;
    buf[idx++] = (0x0000u << 16) | 0x3E00u;
    buf[idx++] = (0x0000u << 16) | 0x0000u;
    buf[idx++] = 0x3E00u;

    /* Translation 1 = (0, 0, 0) */
    buf[idx++] = 0x204C0003;
    buf[idx++] = 0;
    buf[idx++] = 0;

    /* Matrix 2 = 3x3 Identity */
    buf[idx++] = 0x20500009;
    buf[idx++] = (0x0000u << 16) | 0x3E00u;
    buf[idx++] = (0x0000u << 16) | 0x0000u;
    buf[idx++] = (0x0000u << 16) | 0x3E00u;
    buf[idx++] = (0x0000u << 16) | 0x0000u;
    buf[idx++] = 0x3E00u;

    /* Translation 2 = (0, 0, 0) */
    buf[idx++] = 0x205C0003;
    buf[idx++] = 0;
    buf[idx++] = 0;

    /* Near Plane = 0 */
    buf[idx++] = 0x20740001;
    buf[idx++] = 0;

    /* Viewport Scale X, Y = (1.0f, 1.0f) */
    buf[idx++] = 0x20780002;
    buf[idx++] = (0x3E00u << 16) | 0x3E00u;

    *p_idx = idx;
}

static inline void emit_tex_triangle(uint32_t *buf, uint32_t *p_idx,
                                     int x0, int y0, uint16_t z0, uint16_t u0, uint16_t v0,
                                     int x1, int y1, uint16_t z1, uint16_t u1, uint16_t v1,
                                     int x2, int y2, uint16_t z2, uint16_t u2, uint16_t v2)
{
    uint32_t idx = *p_idx;
    /* Opcode 0x44, vtx_fmt 0x10 (textured: 3 words per vertex: X/Y, Z, U/V), 3 vertices */
    buf[idx++] = 0x44100003;
    /* V0 */
    buf[idx++] = ((uint32_t)f16_from_int(y0) << 16) | f16_from_int(x0);
    buf[idx++] = z0;
    buf[idx++] = ((uint32_t)v0 << 16) | u0;
    /* V1 */
    buf[idx++] = ((uint32_t)f16_from_int(y1) << 16) | f16_from_int(x1);
    buf[idx++] = z1;
    buf[idx++] = ((uint32_t)v1 << 16) | u1;
    /* V2 */
    buf[idx++] = ((uint32_t)f16_from_int(y2) << 16) | f16_from_int(x2);
    buf[idx++] = z2;
    buf[idx++] = ((uint32_t)v2 << 16) | u2;

    *p_idx = idx;
}

static inline uint32_t tiled_offset(uint32_t x, uint32_t y)
{
    return (y / 8u) * 8192u + (x / 32u) * 256u + (y % 8u) * 32u + (x % 32u);
}

static void init_texture_atlas(volatile uint16_t *surface)
{
    /* 32x32 texture placed at (origin_x=0, origin_y=240)
     * 4 distinct quadrants of 16x16 texels:
     * Quadrant 0 (TL, u<16, v<16): Red (0x001F)
     * Quadrant 1 (TR, u>=16, v<16): Green (0x03E0)
     * Quadrant 2 (BL, u<16, v>=16): Blue (0x7C00)
     * Quadrant 3 (BR, u>=16, v>=16): Yellow (0x03FF) */
    for (uint32_t v = 0; v < 32; ++v) {
        for (uint32_t u = 0; u < 32; ++u) {
            uint16_t color;
            if (v < 16) {
                color = (u < 16) ? 0x001F : 0x03E0;
            } else {
                color = (u < 16) ? 0x7C00 : 0x03FF;
            }
            surface[tiled_offset(u, 240 + v)] = color;
        }
    }
}

/* Render textured triangle test scene:
 * 1. Background fill (0x181820)
 * 2. Setup 3D pipeline & texture binding (32x32 texture at atlas origin 0, 240)
 * 3. Affine textured quad (triangles 1A & 1B at Z=1.0f): [30..190, 30..190]
 * 4. Perspective-correct foreshortened triangle (varying Z=1.0f at apex, Z=2.0f at base)
 * 5. Coordinate wrapping triangle (UV up to 2.0f) with Scissor clipping [190..300, 160..230]
 */
static void draw_tex_scene(vflash_gpu_t *g, vflash_gpu_surface_t *s)
{
    /* 1. Clear visible playfield only */
    VFLASH_ASSERT(vflash_gpu_fill_rect(g, s, 0, 0, 320, 240, 0x181820) == 0);

    /* 2. Setup 3D pipeline */
    uint32_t idx = 0;
    emit_3d_pipeline_setup(cmd_buffer, &idx);

    /* GE state */
    cmd_buffer[idx++] = 0x81000000;                                       /* Drawing origin (0, 0) */
    cmd_buffer[idx++] = 0x82000000u | (0u << 12) | 0u;                    /* Scissor TL (0, 0) */
    cmd_buffer[idx++] = 0x83000000u | ((240u - 1u) << 12) | (320u - 1u); /* Scissor BR (319, 239) */
    cmd_buffer[idx++] = 0x84000000;                                       /* Lighting disabled */
    cmd_buffer[idx++] = 0x1C000000;                                       /* Culling disabled */
    cmd_buffer[idx++] = 0x1E000000;                                       /* Depth disabled */
    cmd_buffer[idx++] = 0x88000000;                                       /* Blend disabled */
    cmd_buffer[idx++] = 0x96000000;                                       /* Depth clear disabled */

    /* Texture binding:
     * Opcode 0x86: Texture format 16-bit, size 32x32 -> (2 << 3) | 2 = 0x12
     * Opcode 0x87: Texture origin (x=0, y=240) -> (240 << 12) | 0 = 0x000F0000 */
    cmd_buffer[idx++] = 0x86000012;
    cmd_buffer[idx++] = 0x870F0000;

    /* 3. Affine textured quad: 2 triangles (Z = 1.0f) covering [30..190, 30..190] */
    /* Triangle 1A: (30, 30) UV(0,0) - (30, 190) UV(0,1) - (190, 190) UV(1,1) */
    emit_tex_triangle(cmd_buffer, &idx,
                      30, 30, F16_1_0, F16_0_0, F16_0_0,
                      30, 190, F16_1_0, F16_0_0, F16_1_0,
                      190, 190, F16_1_0, F16_1_0, F16_1_0);

    /* Triangle 1B: (30, 30) UV(0,0) - (190, 190) UV(1,1) - (190, 30) UV(1,0) */
    emit_tex_triangle(cmd_buffer, &idx,
                      30, 30, F16_1_0, F16_0_0, F16_0_0,
                      190, 190, F16_1_0, F16_1_0, F16_1_0,
                      190, 30, F16_1_0, F16_1_0, F16_0_0);

    /* 4. Perspective foreshortened triangle:
     * Apex V0: screen (240, 30), Z = 1.0f -> obj (240, 30), UV (0.5, 0.0)
     * Base V1: screen (180, 150), Z = 2.0f -> obj (360, 300), UV (0.0, 1.0)
     * Base V2: screen (300, 150), Z = 2.0f -> obj (600, 300), UV (1.0, 1.0) */
    emit_tex_triangle(cmd_buffer, &idx,
                      240, 30, F16_1_0, F16_0_5, F16_0_0,
                      360, 300, F16_2_0, F16_0_0, F16_1_0,
                      600, 300, F16_2_0, F16_1_0, F16_1_0);

    submit_cmd(s->physical_address, idx);

    /* 5. Coordinate wrapping triangle under Scissor Rect [190..300, 160..230] */
    idx = 0;
    cmd_buffer[idx++] = 0x81000000;
    cmd_buffer[idx++] = 0x82000000u | (160u << 12) | 190u;          /* Scissor TL (190, 160) */
    cmd_buffer[idx++] = 0x83000000u | (230u << 12) | 300u;          /* Scissor BR (300, 230) */
    cmd_buffer[idx++] = 0x84000000;
    cmd_buffer[idx++] = 0x1C000000;
    cmd_buffer[idx++] = 0x1E000000;
    cmd_buffer[idx++] = 0x88000000;
    cmd_buffer[idx++] = 0x96000000;

    cmd_buffer[idx++] = 0x86000012;
    cmd_buffer[idx++] = 0x870F0000;

    /* Triangle spans outside scissor:
     * V0: screen (245, 150), Z=1.0f, UV (1.0, 0.0)
     * V1: screen (180, 230), Z=1.0f, UV (0.0, 2.0)
     * V2: screen (310, 230), Z=1.0f, UV (2.0, 2.0) */
    emit_tex_triangle(cmd_buffer, &idx,
                      245, 150, F16_1_0, F16_1_0, F16_0_0,
                      180, 230, F16_1_0, F16_0_0, F16_2_0,
                      310, 230, F16_1_0, F16_2_0, F16_2_0);

    submit_cmd(s->physical_address, idx);

    /* Restore full-screen scissor */
    idx = 0;
    cmd_buffer[idx++] = 0x81000000;
    cmd_buffer[idx++] = 0x82000000u | (0u << 12) | 0u;
    cmd_buffer[idx++] = 0x83000000u | ((240u - 1u) << 12) | (320u - 1u);
    submit_cmd(s->physical_address, idx);
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDF) ? 0x600D600D : 0;
    boot_marker = 0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());

#ifdef GPU_TRIANGLE_MMU
    VFLASH_ASSERT(vflash_mmu_identity() == 0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000, 0xA8000000, 1) == 0);
    VFLASH_ASSERT(vflash_mmu_enable() == 0);
#endif
    uint32_t c;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(c));
    gpu_triangle_control = c;

    /* Initialize backing surfaces to sentinel 0x255A */
    for (uint32_t i = 0; i < 278528; ++i) {
        triangle_surface_a[i] = triangle_surface_b[i] = 0x255A;
    }

    /* Initialize offscreen 32x32 texture atlas at (0, 240) in both surfaces */
    init_texture_atlas(triangle_surface_a);
    init_texture_atlas(triangle_surface_b);

    vflash_framebuffer_t fb;
    VFLASH_ASSERT(vflash_video_init(&fb));
    video_status = 0x56494431;
    draw_clear(&fb, 0);
    VFLASH_ASSERT(vflash_video_present(&fb));

    vflash_gpu_t g;
    vflash_gpu_surface_t a, b;
    /* Register surfaces with playfield 320x240, but capacity covering all 272 scanlines */
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&a, (void *)triangle_surface_a,
        (uint32_t)(uintptr_t)triangle_surface_a, 320, 240, sizeof(triangle_surface_a)) == 0);
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&b, (void *)triangle_surface_b,
        (uint32_t)(uintptr_t)triangle_surface_b, 320, 240, sizeof(triangle_surface_b)) == 0);
    VFLASH_ASSERT(vflash_gpu_init(&g, VFLASH_GPU_ZEVIO) == 0);

    /* Run 32/33 frames */
    uint32_t target_frames = (GPU_TRIANGLE_FINAL == 0) ? 33 : 32;

    uint16_t begin = vflash_clock_ticks();
    for (uint32_t frame = 0; frame < target_frames; ++frame) {
        vflash_gpu_surface_t *s = (frame & 1) ? &b : &a;

        uint16_t start_op = vflash_clock_ticks();
        draw_tex_scene(&g, s);
        gpu_operation_ticks += (uint16_t)(vflash_clock_ticks() - start_op);

        uint16_t start_pres = vflash_clock_ticks();
        VFLASH_ASSERT(vflash_gpu_present(&g, s, 0, 0) == 0);
        gpu_present_ticks += (uint16_t)(vflash_clock_ticks() - start_pres);

        gpu_triangle_frames = frame + 1;
    }
    gpu_total_ticks = (uint16_t)(vflash_clock_ticks() - begin);

#if GPU_TRIANGLE_FINAL == 2
    /* Linear recovery verification */
    VFLASH_ASSERT(vflash_video_restore_linear() == 1);
#endif

    render_status = 0x44524157;
    gpu_triangle_status = 0x54455831; /* "TEX1" */

    while (1) {
        /* idle */
    }
}
