/* Native Zevio 3D rotating textured cube diagnostic.
 * Validates hardware GE transformation (scale, rotation, translation),
 * perspective projection, depth testing, bilinear texture filtering,
 * and 320x240 double-buffered presentation across multiple frames. */
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
 * (272/8) * 16384 / 2 = 278,528 uint16_t (544 KiB) each. */
volatile uint16_t cube_surface_a[278528] __attribute__((aligned(64)));
volatile uint16_t cube_surface_b[278528] __attribute__((aligned(64)));

volatile uint32_t gpu_cube_status, gpu_cube_frames, gpu_cube_control;
volatile uint32_t gpu_operation_ticks, gpu_present_ticks, gpu_total_ticks;
volatile uint32_t gpu_submission_ticks, gpu_completion_ticks, gpu_lists;

struct cube_vertex {
    uint16_t x, y, z;
    uint16_t u, v;
};

static const int16_t sin_table[256] = {
         0,    402,    804,   1205,   1606,   2006,   2404,   2801,
      3196,   3590,   3981,   4370,   4756,   5139,   5520,   5897,
      6270,   6639,   7005,   7366,   7723,   8076,   8423,   8765,
      9102,   9434,   9760,  10080,  10394,  10702,  11003,  11297,
     11585,  11866,  12140,  12406,  12665,  12916,  13160,  13395,
     13623,  13842,  14053,  14256,  14449,  14635,  14811,  14978,
     15137,  15286,  15426,  15557,  15679,  15791,  15893,  15986,
     16069,  16143,  16207,  16261,  16305,  16340,  16364,  16379,
     16384,  16379,  16364,  16340,  16305,  16261,  16207,  16143,
     16069,  15986,  15893,  15791,  15679,  15557,  15426,  15286,
     15137,  14978,  14811,  14635,  14449,  14256,  14053,  13842,
     13623,  13395,  13160,  12916,  12665,  12406,  12140,  11866,
     11585,  11297,  11003,  10702,  10394,  10080,   9760,   9434,
      9102,   8765,   8423,   8076,   7723,   7366,   7005,   6639,
      6270,   5897,   5520,   5139,   4756,   4370,   3981,   3590,
      3196,   2801,   2404,   2006,   1606,   1205,    804,    402,
         0,   -402,   -804,  -1205,  -1606,  -2006,  -2404,  -2801,
     -3196,  -3590,  -3981,  -4370,  -4756,  -5139,  -5520,  -5897,
     -6270,  -6639,  -7005,  -7366,  -7723,  -8076,  -8423,  -8765,
     -9102,  -9434,  -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395,
    -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986,
    -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143,
    -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842,
    -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080,  -9760,  -9434,
     -9102,  -8765,  -8423,  -8076,  -7723,  -7366,  -7005,  -6639,
     -6270,  -5897,  -5520,  -5139,  -4756,  -4370,  -3981,  -3590,
     -3196,  -2801,  -2404,  -2006,  -1606,  -1205,   -804,   -402,
};

static const struct cube_vertex cube_vertices[36] = {
    /* Face: front */
    {0xBE00, 0xBE00, 0xBE00, 0x0000, 0x0000},
    {0x3E00, 0xBE00, 0xBE00, 0x3A00, 0x0000},
    {0x3E00, 0x3E00, 0xBE00, 0x3A00, 0x3C00},
    {0xBE00, 0xBE00, 0xBE00, 0x0000, 0x0000},
    {0x3E00, 0x3E00, 0xBE00, 0x3A00, 0x3C00},
    {0xBE00, 0x3E00, 0xBE00, 0x0000, 0x3C00},
    /* Face: back */
    {0x3E00, 0xBE00, 0x3E00, 0x3A00, 0x0000},
    {0xBE00, 0xBE00, 0x3E00, 0x3C00, 0x0000},
    {0xBE00, 0x3E00, 0x3E00, 0x3C00, 0x3C00},
    {0x3E00, 0xBE00, 0x3E00, 0x3A00, 0x0000},
    {0xBE00, 0x3E00, 0x3E00, 0x3C00, 0x3C00},
    {0x3E00, 0x3E00, 0x3E00, 0x3A00, 0x3C00},
    /* Face: left */
    {0xBE00, 0xBE00, 0x3E00, 0x3C00, 0x0000},
    {0xBE00, 0xBE00, 0xBE00, 0x3D00, 0x0000},
    {0xBE00, 0x3E00, 0xBE00, 0x3D00, 0x3C00},
    {0xBE00, 0xBE00, 0x3E00, 0x3C00, 0x0000},
    {0xBE00, 0x3E00, 0xBE00, 0x3D00, 0x3C00},
    {0xBE00, 0x3E00, 0x3E00, 0x3C00, 0x3C00},
    /* Face: right */
    {0x3E00, 0xBE00, 0xBE00, 0x3D00, 0x0000},
    {0x3E00, 0xBE00, 0x3E00, 0x3E00, 0x0000},
    {0x3E00, 0x3E00, 0x3E00, 0x3E00, 0x3C00},
    {0x3E00, 0xBE00, 0xBE00, 0x3D00, 0x0000},
    {0x3E00, 0x3E00, 0x3E00, 0x3E00, 0x3C00},
    {0x3E00, 0x3E00, 0xBE00, 0x3D00, 0x3C00},
    /* Face: top */
    {0xBE00, 0xBE00, 0x3E00, 0x0000, 0x3C00},
    {0x3E00, 0xBE00, 0x3E00, 0x3A00, 0x3C00},
    {0x3E00, 0xBE00, 0xBE00, 0x3A00, 0x3E00},
    {0xBE00, 0xBE00, 0x3E00, 0x0000, 0x3C00},
    {0x3E00, 0xBE00, 0xBE00, 0x3A00, 0x3E00},
    {0xBE00, 0xBE00, 0xBE00, 0x0000, 0x3E00},
    /* Face: bottom */
    {0xBE00, 0x3E00, 0xBE00, 0x3A00, 0x3C00},
    {0x3E00, 0x3E00, 0xBE00, 0x3C00, 0x3C00},
    {0x3E00, 0x3E00, 0x3E00, 0x3C00, 0x3E00},
    {0xBE00, 0x3E00, 0xBE00, 0x3A00, 0x3C00},
    {0x3E00, 0x3E00, 0x3E00, 0x3C00, 0x3E00},
    {0xBE00, 0x3E00, 0x3E00, 0x3A00, 0x3E00},
};

static inline uint16_t f16_from_q14(int32_t val)
{
    if (val == 0) return 0;
    uint16_t sign = (val < 0) ? 0x8000 : 0;
    uint32_t u = (val < 0) ? (uint32_t)-val : (uint32_t)val;
    int highest = 31 - __builtin_clz(u);
    int exp_biased = highest - 14 + 31;
    uint32_t mant;
    if (highest >= 9) {
        uint32_t round_bit = (highest >= 10) ? (1u << (highest - 10)) : 0;
        mant = ((u + round_bit) >> (highest - 9)) & 0x1FF;
    } else {
        mant = (u << (9 - highest)) & 0x1FF;
    }
    return (uint16_t)(sign | ((uint32_t)exp_biased << 9) | mant);
}

static inline uint32_t tiled_offset(uint32_t x, uint32_t y)
{
    return (y / 8) * 8192 + (x / 32) * 256 + (y % 8) * 32 + (x % 32);
}

static void init_cube_texture(volatile uint16_t *surface)
{
    for (uint32_t v = 0; v < 32; ++v) {
        for (uint32_t u = 0; u < 64; ++u) {
            uint16_t val = 0x5555;
            if (v < 16) {
                if (u < 16) {
                    /* Slot 0: Front - Red with Yellow border */
                    val = (u < 2 || u >= 14 || v < 2 || v >= 14) ? 0x03FF : 0x001F;
                } else if (u < 32) {
                    /* Slot 1: Back - Blue with Cyan checker */
                    uint32_t lu = u - 16;
                    val = (((lu / 4) ^ (v / 4)) & 1) ? 0x7FE0 : 0x7C00;
                } else if (u < 48) {
                    /* Slot 2: Left - Green with White cross */
                    uint32_t lu = u - 32;
                    val = (lu == 7 || lu == 8 || v == 7 || v == 8) ? 0x7FFF : 0x03E0;
                } else {
                    /* Slot 3: Right - Magenta with Yellow diamond */
                    int32_t lu = (int32_t)(u - 48);
                    int32_t d = (lu >= 8 ? lu - 8 : 7 - lu) + (v >= 8 ? (int32_t)v - 8 : 7 - (int32_t)v);
                    val = (d <= 4) ? 0x03FF : 0x7C1F;
                }
            } else {
                uint32_t lv = v - 16;
                if (u < 16) {
                    /* Slot 4: Top - Blue with White square */
                    val = (u >= 5 && u <= 10 && lv >= 5 && lv <= 10) ? 0x7FFF : 0x7C00;
                } else if (u < 32) {
                    /* Slot 5: Bottom - Yellow with Red stripes */
                    val = ((lv / 2) & 1) ? 0x001F : 0x03FF;
                }
            }
            surface[tiled_offset(u, 240 + v)] = val;
        }
    }
}

static uint32_t cmd_buffer[256] __attribute__((aligned(64)));

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

static void draw_cube_frame(vflash_gpu_t *g, vflash_gpu_surface_t *s, uint32_t frame)
{
    /* 1. Clear playfield color */
    VFLASH_ASSERT(vflash_gpu_fill_rect(g, s, 0, 0, 320, 240, 0x181820) == 0);

    /* 2. Compute 3x3 rotation matrix for frame */
    uint8_t ay = (uint8_t)(frame * 8);
    uint8_t ax = (uint8_t)(frame * 5);
    int32_t sy = sin_table[ay];
    int32_t cy = sin_table[(uint8_t)(ay + 64)];
    int32_t sx = sin_table[ax];
    int32_t cx = sin_table[(uint8_t)(ax + 64)];

    uint16_t m00 = f16_from_q14(cy);
    uint16_t m01 = 0x0000;
    uint16_t m02 = f16_from_q14(sy);
    uint16_t m10 = f16_from_q14((sx * sy) >> 14);
    uint16_t m11 = f16_from_q14(cx);
    uint16_t m12 = f16_from_q14(-(sx * cy) >> 14);
    uint16_t m20 = f16_from_q14(-(cx * sy) >> 14);
    uint16_t m21 = f16_from_q14(sx);
    uint16_t m22 = f16_from_q14((cx * cy) >> 14);

    /* 3. Build depth clear + 3D cube rendering command list */
    uint32_t idx = 0;
    cmd_buffer[idx++] = 0x85000000;                              /* Enable zfill */
    cmd_buffer[idx++] = 0x82000000;                              /* Scissor TL (0, 0) */
    cmd_buffer[idx++] = 0x83000000u | ((240u - 1u) << 12) | (320u - 1u); /* Scissor BR (319, 239) */
    cmd_buffer[idx++] = 0xD3000003;                              /* D3 depth fill */
    cmd_buffer[idx++] = 0;                                       /* Origin (0, 0) */
    cmd_buffer[idx++] = (240u << 16) | 320u;                     /* Size 320x240 */
    cmd_buffer[idx++] = 0;                                       /* Depth clear value 0 -> 1.0e30f */
    cmd_buffer[idx++] = 0x96000000;                              /* Disable zfill */

    cmd_buffer[idx++] = 0x1E008000;                              /* Enable Depth Testing */
    cmd_buffer[idx++] = 0x81000000 | (120u << 12) | 160u;        /* Origin (160, 120) */
    cmd_buffer[idx++] = 0x82000000;                              /* Scissor TL (0, 0) */
    cmd_buffer[idx++] = 0x83000000 | (239u << 12) | 319u;        /* Scissor BR (319, 239) */
    cmd_buffer[idx++] = 0x86000013;                              /* Texture Size 64x32 */
    cmd_buffer[idx++] = 0x87000000 | (240u << 12) | 0u;          /* Texture Origin (0, 240) */
    cmd_buffer[idx++] = 0x1C000000;                              /* Disable Backface Culling */
    cmd_buffer[idx++] = 0x88000000;                              /* Disable Blending */

    /* Model Scales X, Y, Z = 1.0f */
    cmd_buffer[idx++] = 0x203C0003;
    cmd_buffer[idx++] = (0x3E00u << 16) | 0x3E00u;
    cmd_buffer[idx++] = 0x3E00u;

    /* Matrix 1: 3x3 Rotation */
    cmd_buffer[idx++] = 0x20400009;
    cmd_buffer[idx++] = ((uint32_t)m01 << 16) | m00;
    cmd_buffer[idx++] = ((uint32_t)m10 << 16) | m02;
    cmd_buffer[idx++] = ((uint32_t)m12 << 16) | m11;
    cmd_buffer[idx++] = ((uint32_t)m21 << 16) | m20;
    cmd_buffer[idx++] = (uint32_t)m22;

    /* Translation 1: (0, 0, 3.5f) */
    cmd_buffer[idx++] = 0x204C0003;
    cmd_buffer[idx++] = 0;
    cmd_buffer[idx++] = 0x4180u;

    /* Matrix 2: 3x3 Identity */
    cmd_buffer[idx++] = 0x20500009;
    cmd_buffer[idx++] = (0x0000u << 16) | 0x3E00u;
    cmd_buffer[idx++] = (0x0000u << 16) | 0x0000u;
    cmd_buffer[idx++] = (0x0000u << 16) | 0x3E00u;
    cmd_buffer[idx++] = (0x0000u << 16) | 0x0000u;
    cmd_buffer[idx++] = 0x3E00u;

    /* Translation 2: (0, 0, 0) */
    cmd_buffer[idx++] = 0x205C0003;
    cmd_buffer[idx++] = 0;
    cmd_buffer[idx++] = 0;

    /* Near Plane = 0.25f (0x3800) */
    cmd_buffer[idx++] = 0x20740001;
    cmd_buffer[idx++] = 0x3800u;

    /* Viewport Scale X, Y = (160.0f, 160.0f) */
    cmd_buffer[idx++] = 0x20780002;
    cmd_buffer[idx++] = (0x4C80u << 16) | 0x4C80u;

    /* Opcode 0x44: vtx_fmt 0x10 (textured: 3 words per vertex: Y:X, Z, V:U), 36 vertices */
    cmd_buffer[idx++] = 0x44100024;
    for (int i = 0; i < 36; ++i) {
        cmd_buffer[idx++] = ((uint32_t)cube_vertices[i].y << 16) | cube_vertices[i].x;
        cmd_buffer[idx++] = cube_vertices[i].z;
        cmd_buffer[idx++] = ((uint32_t)cube_vertices[i].v << 16) | cube_vertices[i].u;
    }

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
    gpu_cube_control = c;

    /* Initialize backing surfaces to sentinel 0x255A */
    for (uint32_t i = 0; i < 278528; ++i) {
        cube_surface_a[i] = cube_surface_b[i] = 0x255A;
    }

    /* Initialize offscreen 64x32 texture atlas at (0, 240) in both surfaces */
    init_cube_texture(cube_surface_a);
    init_cube_texture(cube_surface_b);

    vflash_framebuffer_t fb;
    VFLASH_ASSERT(vflash_video_init(&fb));
    video_status = 0x56494431;
    draw_clear(&fb, 0);
    VFLASH_ASSERT(vflash_video_present(&fb));

    vflash_gpu_t g;
    vflash_gpu_surface_t a, b;
    /* Register surfaces with playfield 320x240, capacity 272 scanlines */
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&a, (void *)cube_surface_a,
        (uint32_t)(uintptr_t)cube_surface_a, 320, 240, sizeof(cube_surface_a)) == 0);
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&b, (void *)cube_surface_b,
        (uint32_t)(uintptr_t)cube_surface_b, 320, 240, sizeof(cube_surface_b)) == 0);
    VFLASH_ASSERT(vflash_gpu_init(&g, VFLASH_GPU_ZEVIO) == 0);

    /* Run 32/33 frames */
    uint32_t target_frames = (GPU_TRIANGLE_FINAL == 0) ? 33 : 32;

    uint16_t begin = vflash_clock_ticks();
    for (uint32_t frame = 0; frame < target_frames; ++frame) {
        vflash_gpu_surface_t *s = (frame & 1) ? &b : &a;

        uint16_t start_op = vflash_clock_ticks();
        draw_cube_frame(&g, s, frame);
        gpu_operation_ticks += (uint16_t)(vflash_clock_ticks() - start_op);

        uint16_t start_pres = vflash_clock_ticks();
        VFLASH_ASSERT(vflash_gpu_present(&g, s, 0, 0) == 0);
        gpu_present_ticks += (uint16_t)(vflash_clock_ticks() - start_pres);

        gpu_cube_frames = frame + 1;
    }
    gpu_total_ticks = (uint16_t)(vflash_clock_ticks() - begin);

#if GPU_TRIANGLE_FINAL == 2
    /* Linear recovery verification */
    VFLASH_ASSERT(vflash_video_restore_linear() == 1);
#endif

    render_status = 0x44524157;
    gpu_cube_status = 0x43554231; /* "CUB1" */

    while (1) {
        /* idle */
    }
}
