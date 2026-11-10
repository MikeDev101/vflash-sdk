/* High-level 2D renderer demonstration using vflash_renderer_t:
 * manages tiled double-buffering, automatic atlas packing, linear asset uploads,
 * clipping, clearing, and multi-mode sprite dispatch. */
#include "vflash/renderer.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#ifdef GPU_RENDERER_MMU
#include "vflash/mmu.h"
#endif

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint16_t renderer_surface_a[32768] __attribute__((aligned(64)));
volatile uint16_t renderer_surface_b[32768] __attribute__((aligned(64)));
volatile uint16_t renderer_bgr_asset[256] __attribute__((aligned(64)));
volatile uint16_t renderer_abgr_asset[256] __attribute__((aligned(64)));
volatile uint32_t gpu_renderer_status,gpu_renderer_frames,gpu_renderer_control;
volatile uint32_t gpu_operation_ticks,gpu_present_ticks,gpu_total_ticks;
volatile uint32_t gpu_submission_ticks,gpu_completion_ticks,gpu_lists;

static uint16_t bgr_pixel(uint32_t x,uint32_t y)
{
    if ((x<3 && y<3) || (x>=13 && y<3)) return (x==0 && y==0)?0x0000:0x8000;
    if ((x<3 && y>=13) || (x>=13 && y>=13)) return (x==15 && y==15)?0xFFFF:0x9ABC;
    if (x>=6 && x<=9 && y>=6 && y<=9) return ((x+y)&1)?0x8000:0x0000;
    if (x==0 || x==15 || y==0 || y==15) return 0x7FFF;
    if (x==y) return 0x03FF;
    if (x+y==15) return 0x7FE0;
    uint32_t base=(y<8)?((x<8)?0x001Fu:0x03E0u):((x<8)?0x7C00u:0x7C1Fu);
    return (uint16_t)((base ^ (((y&7u)<<5) | (x&7u))) & 0x7FFFu);
}

static uint16_t abgr_pixel(uint32_t x,uint32_t y)
{
    if ((x<2 && y<2) || (x>=14 && y<2) || (x<2 && y>=14) || (x>=14 && y>=14))
        return 0x0FFF;
    if (x>=6 && x<=9 && y>=6 && y<=9) {
        if (y<8) return (x<8)?0xF000:0xFFFF;
        return (x<8)?0x70FF:0x3FF0;
    }
    if (y<8) return (x<8)?0x700F:0x70F0;
    return (x<8)?0x7F00:0xBF0F;
}

void vflash_main(void)
{
    startup_status=boot_marker==0 && data_cookie==0x13579BDF?0x600D600D:0;boot_marker=0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef GPU_RENDERER_MMU
    VFLASH_ASSERT(vflash_mmu_identity()==0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000,0xA8000000,1)==0);
    VFLASH_ASSERT(vflash_mmu_enable()==0);
#endif
    uint32_t c;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(c));gpu_renderer_control=c;
    for (uint32_t i=0;i<32768;++i) renderer_surface_a[i]=renderer_surface_b[i]=0x255A;
    for (uint32_t y=0;y<16;++y) {
        for (uint32_t x=0;x<16;++x) {
            renderer_bgr_asset[y*16+x]=bgr_pixel(x,y);
            renderer_abgr_asset[y*16+x]=abgr_pixel(x,y);
        }
    }
    video_status=0x56494431;

    vflash_renderer_t r;
    vflash_renderer_config_t cfg = {
        .surface_a = (void *)renderer_surface_a,
        .surface_b = (void *)renderer_surface_b,
        .surface_bytes = sizeof(renderer_surface_a),
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

    /* Upload textures into the managed atlas: BGR asset at (96, 8) and ABGR asset at (112, 8) */
    vflash_texture_t tex_bgr_opaque, tex_bgr_keyed, tex_abgr;
    VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &tex_bgr_opaque, (const void *)renderer_bgr_asset, 16, 16, VFLASH_TEXTURE_OPAQUE) == 0);
    /* For the keyed version, reuse the same atlas location by setting type */
    tex_bgr_keyed = tex_bgr_opaque;
    tex_bgr_keyed.type = VFLASH_TEXTURE_KEYED;

    VFLASH_ASSERT(vflash_renderer_upload_texture(&r, &tex_abgr, (const void *)renderer_abgr_asset, 16, 16, VFLASH_TEXTURE_TRANSLUCENT) == 0);

    uint16_t begin=vflash_clock_ticks();
    for (uint32_t frame=0;frame<32;++frame) {
        uint16_t start=vflash_clock_ticks();

        /* Background clearing and fills over the visible 96x32 playfield */
        VFLASH_ASSERT(vflash_renderer_clear(&r, 0x102060) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 0, 16, 96, 16, 0x105020) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 0, 14, 96, 4, 0xC08020) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 30, 0, 4, 32, 0x404040) == 0);
        VFLASH_ASSERT(vflash_renderer_fill_rect(&r, 62, 0, 4, 32, 0x404040) == 0);

        /* Draw 1: Opaque D2 copy of BGR asset to (4, 8) */
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_bgr_opaque, 4, 8) == 0);

        /* Draw 2: Color-keyed C8 sprite of BGR asset to (26, 8) */
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_bgr_keyed, 26, 8) == 0);

        /* Draw 3: Translucent ABGR4444 sprite to (48, 8) */
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_abgr, 48, 8) == 0);

        /* Draw 4: Color-keyed C8 sprite to (70, 8) -- verifies format reset */
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_bgr_keyed, 70, 8) == 0);

        /* Draw 5: Repeated blend ABGR4444 sprite to (52, 12) -- overlaps Draw 3 */
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_abgr, 52, 12) == 0);

        /* Draw 6: Clipped ABGR4444 sprite crossing horizontal and vertical tile boundaries */
        VFLASH_ASSERT(vflash_renderer_set_clip(&r, 24, 6, 20, 16) == 0);
        VFLASH_ASSERT(vflash_renderer_draw(&r, &tex_abgr, 20, 4) == 0);
        VFLASH_ASSERT(vflash_renderer_reset_clip(&r) == 0);

        gpu_operation_ticks+=(uint16_t)(vflash_clock_ticks()-start);
        start=vflash_clock_ticks();
        VFLASH_ASSERT(vflash_renderer_present(&r) == 0);
        gpu_present_ticks+=(uint16_t)(vflash_clock_ticks()-start);
        gpu_renderer_frames=frame+1;
    }
    gpu_total_ticks=(uint16_t)(vflash_clock_ticks()-begin);
    vflash_gpu_stats_t stats;VFLASH_ASSERT(vflash_gpu_stats(&r.gpu,&stats)==0);
    gpu_submission_ticks=stats.submission_ticks;gpu_completion_ticks=stats.completion_ticks;gpu_lists=stats.lists;

    VFLASH_ASSERT(vflash_renderer_shutdown(&r) == 0);
    VFLASH_ASSERT(REG(0xB8000108)==0 && REG(0xB8000160)==(uint32_t)(uintptr_t)r.linear_fb.pixels);

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
    render_status=0x44524157;gpu_renderer_status=0x47524E31; /* "GRN1" */
    for (;;) __asm__ volatile("nop");
}

