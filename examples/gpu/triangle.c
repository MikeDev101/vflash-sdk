/* Native Zevio 3D triangle diagnostic.
 * Validates direct screen-space triangle rasterization (opcode 0x67)
 * in FlashEm: solid red triangle, CCW/CW winding, scissor clipping,
 * and 320x240 double-buffered presentation. */
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

/* 320x240 tiled backing surfaces: (240/8) * 16384 / 2 = 245,760 uint16_t (480 KiB) each */
volatile uint16_t triangle_surface_a[245760] __attribute__((aligned(64)));
volatile uint16_t triangle_surface_b[245760] __attribute__((aligned(64)));

volatile uint32_t gpu_triangle_status, gpu_triangle_frames, gpu_triangle_control;
volatile uint32_t gpu_operation_ticks, gpu_present_ticks, gpu_total_ticks;
volatile uint32_t gpu_submission_ticks, gpu_completion_ticks, gpu_lists;

/* Helper to convert positive integer to Zevio 16-bit float (f16):
 * 1 sign bit (0), 6 exponent bits (bias 31), 9 mantissa bits.
 * Uses native ARM clz instruction without floating-point library dependencies. */
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

static uint32_t cmd_buffer[64] __attribute__((aligned(64)));

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

/* Render the test triangles into the target surface:
 * 1. Background fill (dark blue-gray, 0x181820)
 * 2. Setup Zevio 3D pipeline (scales, identity matrices, viewport scale) via opcode 0x20
 * 3. Primary solid Red triangle (CCW winding): (160, 40), (80, 180), (240, 180)
 * 4. Green triangle (CW winding): (30, 30), (90, 30), (60, 90)
 * 5. Blue scissor-clipped triangle: crossing scissor rect [200..299, 20..119]
 */
static void draw_triangles_scene(vflash_gpu_t *g, vflash_gpu_surface_t *s)
{
    /* 1. Clear surface using 2D fill */
    VFLASH_ASSERT(vflash_gpu_fill_rect(g, s, 0, 0, 320, 240, 0x181820) == 0);

    /* 2. Configure 3D pipeline and submit Triangles 1 & 2 under full-screen scissor */
    uint32_t idx = 0;

    /* Opcode 0x20: Scale X, Y, Z (reg 0x3C, count 3) = 1.0f (f16 0x3E00) */
    cmd_buffer[idx++] = 0x203C0003;
    cmd_buffer[idx++] = (0x3E00u << 16) | 0x3E00u;
    cmd_buffer[idx++] = 0x3E00u;

    /* Opcode 0x20: Matrix 1 = 3x3 Identity (reg 0x40, count 9) */
    cmd_buffer[idx++] = 0x20400009;
    cmd_buffer[idx++] = (0x0000u << 16) | 0x3E00u; /* M01, M00 */
    cmd_buffer[idx++] = (0x0000u << 16) | 0x0000u; /* M10, M02 */
    cmd_buffer[idx++] = (0x0000u << 16) | 0x3E00u; /* M12, M11 */
    cmd_buffer[idx++] = (0x0000u << 16) | 0x0000u; /* M21, M20 */
    cmd_buffer[idx++] = 0x3E00u;                    /* M22 */

    /* Opcode 0x20: Translation 1 = (0, 0, 0) (reg 0x4C, count 3) */
    cmd_buffer[idx++] = 0x204C0003;
    cmd_buffer[idx++] = 0;
    cmd_buffer[idx++] = 0;

    /* Opcode 0x20: Matrix 2 = 3x3 Identity (reg 0x50, count 9) */
    cmd_buffer[idx++] = 0x20500009;
    cmd_buffer[idx++] = (0x0000u << 16) | 0x3E00u; /* M'01, M'00 */
    cmd_buffer[idx++] = (0x0000u << 16) | 0x0000u; /* M'10, M'02 */
    cmd_buffer[idx++] = (0x0000u << 16) | 0x3E00u; /* M'12, M'11 */
    cmd_buffer[idx++] = (0x0000u << 16) | 0x0000u; /* M'21, M'20 */
    cmd_buffer[idx++] = 0x3E00u;                    /* M'22 */

    /* Opcode 0x20: Translation 2 = (0, 0, 0) (reg 0x5C, count 3) */
    cmd_buffer[idx++] = 0x205C0003;
    cmd_buffer[idx++] = 0;
    cmd_buffer[idx++] = 0;

    /* Opcode 0x20: Near Plane = 0 (reg 0x74, count 1) */
    cmd_buffer[idx++] = 0x20740001;
    cmd_buffer[idx++] = 0;

    /* Opcode 0x20: Viewport Scale X, Y = (1.0f, 1.0f) (reg 0x78, count 2) */
    cmd_buffer[idx++] = 0x20780002;
    cmd_buffer[idx++] = (0x3E00u << 16) | 0x3E00u;

    /* GE State */
    cmd_buffer[idx++] = 0x81000000;                                       /* Drawing origin (0, 0) */
    cmd_buffer[idx++] = 0x82000000u | (0u << 12) | 0u;                    /* Scissor top-left (0, 0) */
    cmd_buffer[idx++] = 0x83000000u | ((240u - 1u) << 12) | (320u - 1u); /* Scissor bottom-right (319, 239) */
    cmd_buffer[idx++] = 0x84000000;                                       /* Lighting disabled */
    cmd_buffer[idx++] = 0x1C000000;                                       /* Culling disabled */
    cmd_buffer[idx++] = 0x1E000000;                                       /* Depth disabled */
    cmd_buffer[idx++] = 0x88000000;                                       /* Blend disabled */
    cmd_buffer[idx++] = 0x96000000;                                       /* Depth clear disabled */

    /* Triangle 1: Solid Red (CCW) */
    /* opcode 0x44, vtx_fmt 0x08 (has 16-bit color at word 2), 3 vertices */
    cmd_buffer[idx++] = 0x44080003;
    /* V0: (160, 40) */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(40) << 16) | f16_from_int(160);
    cmd_buffer[idx++] = 0x3E00; /* Z = 1.0f */
    cmd_buffer[idx++] = 0x001F; /* BGR555 Red */
    /* V1: (80, 180) */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(180) << 16) | f16_from_int(80);
    cmd_buffer[idx++] = 0x3E00;
    cmd_buffer[idx++] = 0x001F;
    /* V2: (240, 180) */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(180) << 16) | f16_from_int(240);
    cmd_buffer[idx++] = 0x3E00;
    cmd_buffer[idx++] = 0x001F;

    /* Triangle 2: Solid Green (CW winding to test culling) */
    cmd_buffer[idx++] = 0x44080003;
    /* V0: (30, 30) */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(30) << 16) | f16_from_int(30);
    cmd_buffer[idx++] = 0x3E00;
    cmd_buffer[idx++] = 0x03E0; /* BGR555 Green */
    /* V1: (90, 30) */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(30) << 16) | f16_from_int(90);
    cmd_buffer[idx++] = 0x3E00;
    cmd_buffer[idx++] = 0x03E0;
    /* V2: (60, 90) */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(90) << 16) | f16_from_int(60);
    cmd_buffer[idx++] = 0x3E00;
    cmd_buffer[idx++] = 0x03E0;

    submit_cmd(s->physical_address, idx);

    /* 3. Triangle 3: Solid Blue (0x7C00) with active Scissor Rect [200..299, 20..119] */
    idx = 0;
    cmd_buffer[idx++] = 0x81000000;
    cmd_buffer[idx++] = 0x82000000u | (20u << 12) | 200u;           /* Scissor top-left (200, 20) */
    cmd_buffer[idx++] = 0x83000000u | (119u << 12) | 299u;          /* Scissor bottom-right (299, 119) */
    cmd_buffer[idx++] = 0x84000000;
    cmd_buffer[idx++] = 0x1C000000;
    cmd_buffer[idx++] = 0x1E000000;
    cmd_buffer[idx++] = 0x88000000;
    cmd_buffer[idx++] = 0x96000000;

    /* Triangle spans (180, 50) to (320, 50) and down to (250, 140) */
    cmd_buffer[idx++] = 0x44080003;
    /* V0: (250, 30) */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(30) << 16) | f16_from_int(250);
    cmd_buffer[idx++] = 0x3E00;
    cmd_buffer[idx++] = 0x7C00; /* BGR555 Blue */
    /* V1: (180, 140) - spans outside left and bottom */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(140) << 16) | f16_from_int(180);
    cmd_buffer[idx++] = 0x3E00;
    cmd_buffer[idx++] = 0x7C00;
    /* V2: (320, 140) - spans outside right and bottom */
    cmd_buffer[idx++] = ((uint32_t)f16_from_int(140) << 16) | f16_from_int(320);
    cmd_buffer[idx++] = 0x3E00;
    cmd_buffer[idx++] = 0x7C00;

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

    /* Initialize backing surfaces to known sentinel pattern */
    for (uint32_t i = 0; i < 245760; ++i) {
        triangle_surface_a[i] = triangle_surface_b[i] = 0x255A;
    }

    vflash_framebuffer_t fb;
    VFLASH_ASSERT(vflash_video_init(&fb));
    video_status = 0x56494431;
    draw_clear(&fb, 0);
    VFLASH_ASSERT(vflash_video_present(&fb));

    vflash_gpu_t g;
    vflash_gpu_surface_t a, b;
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
        draw_triangles_scene(&g, s);
        gpu_operation_ticks += (uint16_t)(vflash_clock_ticks() - start_op);

        uint16_t start_pres = vflash_clock_ticks();
        VFLASH_ASSERT(vflash_gpu_present(&g, s, 0, 0) == 0);
        gpu_present_ticks += (uint16_t)(vflash_clock_ticks() - start_pres);

        gpu_triangle_frames = frame + 1;
    }
    gpu_total_ticks = (uint16_t)(vflash_clock_ticks() - begin);

#if GPU_TRIANGLE_FINAL == 2
    /* Linear recovery verification: restore linear scanout mode */
    VFLASH_ASSERT(vflash_video_restore_linear() == 1);
#endif

    render_status = 0x44524157;
    gpu_triangle_status = 0x54524931; /* "TRI1" */

    while (1) {
        /* idle */
    }
}
