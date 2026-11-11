/* Native Zevio 3D depth testing diagnostic.
 * Validates Zevio depth testing (opcode 0x1E008000), depth clearing
 * (opcode 0x85000000 + D3 + 0x96000000), depth comparison direction
 * (Z_new < Z_buf passes), order independence, and painter's order control. */
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

/* Constants for f16 depth values */
#define F16_DEPTH_1_0 0x3E00u  /* 1.0f (closer) */
#define F16_DEPTH_2_0 0x4000u  /* 2.0f (farther) */
#define F16_DEPTH_3_0 0x4100u  /* 3.0f (even farther) */

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
    /* Opcode 0x20: Scale X, Y, Z (reg 0x3C, count 3) = 1.0f (f16 0x3E00) */
    buf[idx++] = 0x203C0003;
    buf[idx++] = (0x3E00u << 16) | 0x3E00u;
    buf[idx++] = 0x3E00u;

    /* Opcode 0x20: Matrix 1 = 3x3 Identity (reg 0x40, count 9) */
    buf[idx++] = 0x20400009;
    buf[idx++] = (0x0000u << 16) | 0x3E00u; /* M01, M00 */
    buf[idx++] = (0x0000u << 16) | 0x0000u; /* M10, M02 */
    buf[idx++] = (0x0000u << 16) | 0x3E00u; /* M12, M11 */
    buf[idx++] = (0x0000u << 16) | 0x0000u; /* M21, M20 */
    buf[idx++] = 0x3E00u;                    /* M22 */

    /* Opcode 0x20: Translation 1 = (0, 0, 0) (reg 0x4C, count 3) */
    buf[idx++] = 0x204C0003;
    buf[idx++] = 0;
    buf[idx++] = 0;

    /* Opcode 0x20: Matrix 2 = 3x3 Identity (reg 0x50, count 9) */
    buf[idx++] = 0x20500009;
    buf[idx++] = (0x0000u << 16) | 0x3E00u; /* M'01, M'00 */
    buf[idx++] = (0x0000u << 16) | 0x0000u; /* M'10, M'02 */
    buf[idx++] = (0x0000u << 16) | 0x3E00u; /* M'12, M'11 */
    buf[idx++] = (0x0000u << 16) | 0x0000u; /* M'21, M'20 */
    buf[idx++] = 0x3E00u;                    /* M'22 */

    /* Opcode 0x20: Translation 2 = (0, 0, 0) (reg 0x5C, count 3) */
    buf[idx++] = 0x205C0003;
    buf[idx++] = 0;
    buf[idx++] = 0;

    /* Opcode 0x20: Near Plane = 0 (reg 0x74, count 1) */
    buf[idx++] = 0x20740001;
    buf[idx++] = 0;

    /* Opcode 0x20: Viewport Scale X, Y = (1.0f, 1.0f) (reg 0x78, count 2) */
    buf[idx++] = 0x20780002;
    buf[idx++] = (0x3E00u << 16) | 0x3E00u;

    *p_idx = idx;
}

static inline void emit_triangle(uint32_t *buf, uint32_t *p_idx,
                                 int x0, int y0, uint16_t z0,
                                 int x1, int y1, uint16_t z1,
                                 int x2, int y2, uint16_t z2,
                                 uint16_t bgr555_color)
{
    uint32_t idx = *p_idx;
    buf[idx++] = 0x44080003; /* opcode 0x44, vtx_fmt 0x08, 3 vertices */
    buf[idx++] = ((uint32_t)f16_from_int(y0) << 16) | f16_from_int(x0);
    buf[idx++] = z0;
    buf[idx++] = bgr555_color;

    buf[idx++] = ((uint32_t)f16_from_int(y1) << 16) | f16_from_int(x1);
    buf[idx++] = z1;
    buf[idx++] = bgr555_color;

    buf[idx++] = ((uint32_t)f16_from_int(y2) << 16) | f16_from_int(x2);
    buf[idx++] = z2;
    buf[idx++] = bgr555_color;
    *p_idx = idx;
}

/* Render depth test scene:
 * 1. 2D clear color surface to dark blue-gray (0x181820).
 * 2. Clear depth buffer to 1.0e30f using opcode 0x85000000 + D3 (depth fill).
 * 3. Region 1 (Left, X < 160): Depth-tested overlapping triangles.
 *    - Triangle Red (far): Z = 2.0f, BGR555 0x001F, (80, 40)-(20, 160)-(140, 160)
 *    - Triangle Green (near): Z = 1.0f, BGR555 0x03E0, (120, 70)-(60, 190)-(180, 190)
 *    - If order == 0: Submit Red first, then Green second.
 *    - If order == 1: Submit Green first, then Red second.
 *    - Depth test: Z_new < Z_buf passes. In both orders, Green is in front of Red!
 * 4. Region 2 (Right, X >= 160): Depth-disabled painter's order control.
 *    - Triangle Blue: Z = 2.0f, BGR555 0x7C00, (230, 40)-(170, 160)-(290, 160), submitted 1st.
 *    - Triangle Yellow: Z = 3.0f, BGR555 0x03FF, (270, 70)-(210, 190)-(330, 190), submitted 2nd.
 *    - Depth disabled: Yellow overwrites Blue despite having greater depth!
 */
static void draw_depth_scene(vflash_gpu_t *g, vflash_gpu_surface_t *s, uint32_t order)
{
    /* 1. Clear surface color */
    VFLASH_ASSERT(vflash_gpu_fill_rect(g, s, 0, 0, 320, 240, 0x181820) == 0);

    /* 2. Clear depth buffer across the entire 320x240 screen */
    uint32_t idx = 0;
    cmd_buffer[idx++] = 0x85000000; /* enable zfill */
    cmd_buffer[idx++] = 0x82000000; /* scissor TL (0, 0) */
    cmd_buffer[idx++] = 0x83000000u | ((240u - 1u) << 12) | (320u - 1u); /* scissor BR (319, 239) */
    cmd_buffer[idx++] = 0xD3000003; /* D3 depth fill */
    cmd_buffer[idx++] = 0;          /* (y=0 << 16) | (x=0) */
    cmd_buffer[idx++] = (240u << 16) | 320u; /* h=240, w=320 */
    cmd_buffer[idx++] = 0;          /* depth clear value 0 -> 1.0e30f */
    cmd_buffer[idx++] = 0x96000000; /* disable zfill */

    /* 3. Setup 3D transform pipeline */
    emit_3d_pipeline_setup(cmd_buffer, &idx);

    /* Common GE state */
    cmd_buffer[idx++] = 0x81000000; /* Drawing origin (0, 0) */
    cmd_buffer[idx++] = 0x82000000u | (0u << 12) | 0u;
    cmd_buffer[idx++] = 0x83000000u | ((240u - 1u) << 12) | (320u - 1u);
    cmd_buffer[idx++] = 0x84000000; /* Lighting disabled */
    cmd_buffer[idx++] = 0x1C000000; /* Culling disabled */
    cmd_buffer[idx++] = 0x88000000; /* Blend disabled */

    /* Enable depth test */
    cmd_buffer[idx++] = 0x1E008000;

    /* Render Region 1: Depth-tested overlapping triangles */
    if (order == 0) {
        /* Far triangle first (Z=2.0f, obj coords = screen * 2), then near triangle second (Z=1.0f) */
        emit_triangle(cmd_buffer, &idx,
                      80 * 2, 40 * 2, F16_DEPTH_2_0,
                      20 * 2, 160 * 2, F16_DEPTH_2_0,
                      140 * 2, 160 * 2, F16_DEPTH_2_0,
                      0x001F); /* Red */

        emit_triangle(cmd_buffer, &idx,
                      120, 70, F16_DEPTH_1_0,
                      60, 190, F16_DEPTH_1_0,
                      180, 190, F16_DEPTH_1_0,
                      0x03E0); /* Green */
    } else {
        /* Near triangle first, then far triangle second */
        emit_triangle(cmd_buffer, &idx,
                      120, 70, F16_DEPTH_1_0,
                      60, 190, F16_DEPTH_1_0,
                      180, 190, F16_DEPTH_1_0,
                      0x03E0); /* Green */

        emit_triangle(cmd_buffer, &idx,
                      80 * 2, 40 * 2, F16_DEPTH_2_0,
                      20 * 2, 160 * 2, F16_DEPTH_2_0,
                      140 * 2, 160 * 2, F16_DEPTH_2_0,
                      0x001F); /* Red */
    }

    /* Disable depth test for Region 2: Painter's order control */
    cmd_buffer[idx++] = 0x1E000000;

    /* Blue triangle (Z=2.0f, obj coords = screen * 2) submitted 1st */
    emit_triangle(cmd_buffer, &idx,
                  230 * 2, 40 * 2, F16_DEPTH_2_0,
                  170 * 2, 160 * 2, F16_DEPTH_2_0,
                  290 * 2, 160 * 2, F16_DEPTH_2_0,
                  0x7C00); /* Blue */

    /* Yellow triangle (Z=3.0f, obj coords = screen * 3, farther) submitted 2nd - overwrites Blue! */
    emit_triangle(cmd_buffer, &idx,
                  270 * 3, 70 * 3, F16_DEPTH_3_0,
                  210 * 3, 190 * 3, F16_DEPTH_3_0,
                  330 * 3, 190 * 3, F16_DEPTH_3_0,
                  0x03FF); /* Yellow */

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

        /* Alternate submission order between frames:
         * Surface A (even frame index): order 0 (Red far first, Green near second)
         * Surface B (odd frame index): order 1 (Green near first, Red far second) */
        uint32_t order = frame & 1;

        uint16_t start_op = vflash_clock_ticks();
        draw_depth_scene(&g, s, order);
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
    gpu_triangle_status = 0x5A444550; /* "ZDEP" */

    while (1) {
        /* idle */
    }
}
