/* Native 3-mode sprite demonstration alternating opaque (D2),
 * color-keyed (0xC8 BGR555_KEY), and translucent (0xC8 ABGR4444) sprites
 * in the same frame on a shared 128x32 tiled backing allocation:
 * x=0..95 is the visible 96x32 playfield, x=96..127 is the offscreen 32x32
 * sprite atlas containing BGR555 and ABGR4444 assets uploaded during init. */
#include "vflash/gpu.h"
#include "vflash/video.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#ifdef GPU_MODES_MMU
#include "vflash/mmu.h"
#endif

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint16_t modes_surface_a[32768] __attribute__((aligned(64)));
volatile uint16_t modes_surface_b[32768] __attribute__((aligned(64)));
volatile uint16_t modes_bgr_asset[256] __attribute__((aligned(64)));
volatile uint16_t modes_abgr_asset[256] __attribute__((aligned(64)));
volatile uint32_t gpu_modes_status,gpu_modes_frames,gpu_modes_uploads,gpu_modes_control;
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
    /* Transparent corners with non-zero color nibbles to verify a=0 skips entirely */
    if ((x<2 && y<2) || (x>=14 && y<2) || (x<2 && y>=14) || (x>=14 && y>=14))
        return 0x0FFF;
    /* Center 4x4 test pattern */
    if (x>=6 && x<=9 && y>=6 && y<=9) {
        if (y<8) return (x<8)?0xF000:0xFFFF; /* opaque black (0xF000) vs opaque white (0xFFFF) */
        return (x<8)?0x70FF:0x3FF0;          /* 50% yellow vs 25% cyan */
    }
    /* Four colored quadrants */
    if (y<8) return (x<8)?0x700F:0x70F0;     /* 50% red vs 50% green */
    return (x<8)?0x7F00:0xBF0F;             /* 50% blue vs 75% magenta */
}

static void fill(vflash_gpu_t *g,vflash_gpu_surface_t *s,int x,int y,int w,int h,uint32_t c)
{
    VFLASH_ASSERT(vflash_gpu_fill_rect(g,s,x,y,w,h,c)==0);
}

void vflash_main(void)
{
    startup_status=boot_marker==0 && data_cookie==0x13579BDF?0x600D600D:0;boot_marker=0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef GPU_MODES_MMU
    VFLASH_ASSERT(vflash_mmu_identity()==0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000,0xA8000000,1)==0);
    VFLASH_ASSERT(vflash_mmu_enable()==0);
#endif
    uint32_t c;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(c));gpu_modes_control=c;
    for (uint32_t i=0;i<32768;++i) modes_surface_a[i]=modes_surface_b[i]=0x255A;
    for (uint32_t y=0;y<16;++y) {
        for (uint32_t x=0;x<16;++x) {
            modes_bgr_asset[y*16+x]=bgr_pixel(x,y);
            modes_abgr_asset[y*16+x]=abgr_pixel(x,y);
        }
    }
    vflash_framebuffer_t fb;VFLASH_ASSERT(vflash_video_init(&fb));video_status=0x56494431;
    draw_clear(&fb,0);VFLASH_ASSERT(vflash_video_present(&fb));
    vflash_gpu_t g;vflash_gpu_surface_t a,b,asset_bgr,asset_abgr;
    vflash_framebuffer_t bgr_fb={(uint16_t *)modes_bgr_asset,16,16,16,256,VFLASH_BGR555};
    vflash_framebuffer_t abgr_fb={(uint16_t *)modes_abgr_asset,16,16,16,256,VFLASH_BGR555};
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&a,(void *)modes_surface_a,(uint32_t)(uintptr_t)modes_surface_a,128,32,sizeof(modes_surface_a))==0);
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&b,(void *)modes_surface_b,(uint32_t)(uintptr_t)modes_surface_b,128,32,sizeof(modes_surface_b))==0);
    VFLASH_ASSERT(vflash_gpu_surface_register(&asset_bgr,&bgr_fb,(uint32_t)(uintptr_t)modes_bgr_asset,1)==0);
    VFLASH_ASSERT(vflash_gpu_surface_register(&asset_abgr,&abgr_fb,(uint32_t)(uintptr_t)modes_abgr_asset,1)==0);
    VFLASH_ASSERT(vflash_gpu_init(&g,VFLASH_GPU_ZEVIO)==0);
    vflash_gpu_caps_t caps=vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_ZEVIO_TILED);
    VFLASH_ASSERT(caps.hardware_accelerated && caps.fill_rect && caps.present && caps.same_surface_copy && caps.upload && caps.color_key_sprite && caps.alpha_blend && !caps.blit);

    /* Validate argument and overlap rejections before submitting lists */
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,99,0,0,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,-1,8,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,112,-1,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,112,8,-1,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,112,8,16,-1,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,120,8,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,112,20,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,112,8,16,16,108,12)==VFLASH_GPU_UNSUPPORTED);
    /* Zero-size or completely clipped sprites are no-ops and do not submit lists */
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,112,8,0,16,8,8)==0);
    VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&a,VFLASH_GPU_TEXTURE_ABGR4444,112,8,16,16,-16,8)==0);

    /* Upload assets into the offscreen atlas (x=96..127, y=0..31) once per A/B surface */
    VFLASH_ASSERT(vflash_gpu_upload(&g,&a,&asset_bgr,0,0,16,16,96,8)==0);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&b,&asset_bgr,0,0,16,16,96,8)==0);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&a,&asset_abgr,0,0,16,16,112,8)==0);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&b,&asset_abgr,0,0,16,16,112,8)==0);
    gpu_modes_uploads=4;

    uint16_t begin=vflash_clock_ticks();
    for (uint32_t frame=0;frame<32;++frame) {
        vflash_gpu_surface_t *s=(frame&1)?&b:&a;
        uint16_t start=vflash_clock_ticks();

        /* Background fills over visible playfield 96x32 */
        fill(&g,s,0,0,96,16,0x102060);
        fill(&g,s,0,16,96,16,0x105020);
        fill(&g,s,0,14,96,4,0xC08020);
        fill(&g,s,30,0,4,32,0x404040);
        fill(&g,s,62,0,4,32,0x404040);

        /* Draw 1: Opaque D2 copy from atlas (96, 8) to (4, 8) */
        VFLASH_ASSERT(vflash_gpu_copy_region(&g,s,96,8,16,16,4,8)==0);

        /* Draw 2: Color-keyed C8 sprite from atlas (96, 8) to (26, 8) */
        VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,s,VFLASH_GPU_TEXTURE_BGR555_KEY,96,8,16,16,26,8)==0);

        /* Draw 3: Translucent ABGR4444 sprite from atlas (112, 8) to (48, 8) */
        VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,s,VFLASH_GPU_TEXTURE_ABGR4444,112,8,16,16,48,8)==0);

        /* Draw 4: Color-keyed C8 sprite from atlas (96, 8) to (70, 8) -- verifies format reset */
        VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,s,VFLASH_GPU_TEXTURE_BGR555_KEY,96,8,16,16,70,8)==0);

        /* Draw 5: Repeated blend ABGR4444 from atlas (112, 8) to (52, 12) -- overlaps Draw 3 and switches back to ABGR4444 */
        VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,s,VFLASH_GPU_TEXTURE_ABGR4444,112,8,16,16,52,12)==0);

        /* Draw 6: Clipped ABGR4444 sprite crossing horizontal (y=8,16) and vertical (x=32) tile boundaries */
        VFLASH_ASSERT(vflash_gpu_clip(s,24,6,20,16)==0);
        VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,s,VFLASH_GPU_TEXTURE_ABGR4444,112,8,16,16,20,4)==0);
        VFLASH_ASSERT(vflash_gpu_clip(s,0,0,128,32)==0);

        VFLASH_ASSERT(vflash_gpu_wait_idle(&g)==0);
        gpu_operation_ticks+=(uint16_t)(vflash_clock_ticks()-start);
        start=vflash_clock_ticks();
        VFLASH_ASSERT(vflash_gpu_present_view(&g,s,96,32,16,24)==0);
        gpu_present_ticks+=(uint16_t)(vflash_clock_ticks()-start);

        /* Reject writes to the displayed buffer and through alias */
        VFLASH_ASSERT(vflash_gpu_fill_rect(&g,s,0,0,1,1,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_copy_region(&g,s,96,8,16,16,0,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,s,96,8,16,16,0,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,s,VFLASH_GPU_TEXTURE_ABGR4444,112,8,16,16,0,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_upload(&g,s,&asset_bgr,0,0,16,16,96,8)==VFLASH_GPU_BUSY);
        vflash_gpu_surface_t alias=*s;
        alias.fb.pixels+=24576;alias.physical_address+=49152;
        alias.fb.height=8;alias.fb.capacity_pixels=8192;alias.clip_h=8;
        VFLASH_ASSERT(vflash_gpu_fill_rect(&g,&alias,0,0,1,1,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&alias,0,0,8,8,16,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_draw_sprite_ex(&g,&alias,VFLASH_GPU_TEXTURE_ABGR4444,0,0,8,8,16,0)==VFLASH_GPU_BUSY);
        gpu_modes_frames=frame+1;
    }
    gpu_total_ticks=(uint16_t)(vflash_clock_ticks()-begin);
    vflash_gpu_stats_t stats;VFLASH_ASSERT(vflash_gpu_stats(&g,&stats)==0);
    gpu_submission_ticks=stats.submission_ticks;gpu_completion_ticks=stats.completion_ticks;gpu_lists=stats.lists;
    VFLASH_ASSERT(vflash_gpu_shutdown(&g)==0);
    VFLASH_ASSERT(REG(0xB8000108)==0 && REG(0xB8000160)==(uint32_t)(uintptr_t)fb.pixels);
    VFLASH_ASSERT(vflash_gpu_wait_idle(&g)==VFLASH_GPU_NOT_INITIALIZED);
    VFLASH_ASSERT(vflash_gpu_init(&g,VFLASH_GPU_ZEVIO)==0);
#if GPU_MODES_FINAL == 0
    VFLASH_ASSERT(vflash_gpu_present_view(&g,&a,96,32,0,0)==0);
#elif GPU_MODES_FINAL == 1
    VFLASH_ASSERT(vflash_gpu_present_view(&g,&b,96,32,224,208)==0);
#else
    VFLASH_ASSERT(vflash_gpu_shutdown(&g)==0);
#endif
    VFLASH_ASSERT(vflash_panic_vectors_intact());
    render_status=0x44524157;gpu_modes_status=0x474D5331;
    for (;;) __asm__ volatile("nop");
}

