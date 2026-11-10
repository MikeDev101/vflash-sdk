/* Native transparent-sprite diagnostic using shared 128x32 tiled backing
 * allocations: x=0..95 is the visible 96x32 playfield, x=96..127 is the
 * offscreen 32x32 sprite atlas uploaded once during initialization.
 * Exercises 0xC8 color-key transparency (both bit-15 0x8000/0xFFFF and 0x0000),
 * surface clipping, negative/offscreen coordinates, and fast-path tex_setup. */
#include "vflash/gpu.h"
#include "vflash/video.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#ifdef GPU_TRANSPARENT_MMU
#include "vflash/mmu.h"
#endif
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint16_t transparent_surface_a[32768] __attribute__((aligned(64)));
volatile uint16_t transparent_surface_b[32768] __attribute__((aligned(64)));
volatile uint16_t transparent_asset[256] __attribute__((aligned(64)));
volatile uint32_t gpu_transparent_status,gpu_transparent_frames,gpu_transparent_uploads,gpu_transparent_control;
volatile uint32_t gpu_operation_ticks,gpu_present_ticks,gpu_total_ticks;
volatile uint32_t gpu_submission_ticks,gpu_completion_ticks,gpu_lists;

static uint16_t transparent_pixel(uint32_t x,uint32_t y)
{
    /* Outer corners are transparent: bit-15 set (0x8000, 0xFFFF, 0x9ABC) and 0x0000. */
    if ((x<3 && y<3) || (x>=13 && y<3)) return (x==0 && y==0)?0x0000:0x8000;
    if ((x<3 && y>=13) || (x>=13 && y>=13)) return (x==15 && y==15)?0xFFFF:0x9ABC;
    /* Transparent center window (6..9, 6..9) mixing 0x8000 and 0x0000. */
    if (x>=6 && x<=9 && y>=6 && y<=9) return ((x+y)&1)?0x8000:0x0000;
    /* Opaque border and diagonals around the transparent cutouts. */
    if (x==0 || x==15 || y==0 || y==15) return 0x7FFF;
    if (x==y) return 0x03FF;
    if (x+y==15) return 0x7FE0;
    uint32_t base=(y<8)?((x<8)?0x001Fu:0x03E0u):((x<8)?0x7C00u:0x7C1Fu);
    return (uint16_t)((base ^ (((y&7u)<<5) | (x&7u))) & 0x7FFFu);
}

static void fill(vflash_gpu_t *g,vflash_gpu_surface_t *s,int x,int y,int w,int h,uint32_t c)
{
    VFLASH_ASSERT(vflash_gpu_fill_rect(g,s,x,y,w,h,c)==0);
}

void vflash_main(void)
{
    startup_status=boot_marker==0 && data_cookie==0x13579BDF?0x600D600D:0;boot_marker=0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef GPU_TRANSPARENT_MMU
    VFLASH_ASSERT(vflash_mmu_identity()==0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000,0xA8000000,1)==0);
    VFLASH_ASSERT(vflash_mmu_enable()==0);
#endif
    uint32_t c;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(c));gpu_transparent_control=c;
    for (uint32_t i=0;i<32768;++i) transparent_surface_a[i]=transparent_surface_b[i]=0x255A;
    for (uint32_t y=0;y<16;++y)
        for (uint32_t x=0;x<16;++x)
            transparent_asset[y*16+x]=transparent_pixel(x,y);
    vflash_framebuffer_t fb;VFLASH_ASSERT(vflash_video_init(&fb));video_status=0x56494431;
    draw_clear(&fb,0);VFLASH_ASSERT(vflash_video_present(&fb));
    vflash_gpu_t g;vflash_gpu_surface_t a,b,asset;
    vflash_framebuffer_t asset_fb={(uint16_t *)transparent_asset,16,16,16,256,VFLASH_BGR555};
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&a,(void *)transparent_surface_a,(uint32_t)(uintptr_t)transparent_surface_a,128,32,sizeof(transparent_surface_a))==0);
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&b,(void *)transparent_surface_b,(uint32_t)(uintptr_t)transparent_surface_b,128,32,sizeof(transparent_surface_b))==0);
    VFLASH_ASSERT(vflash_gpu_surface_register(&asset,&asset_fb,(uint32_t)(uintptr_t)transparent_asset,1)==0);
    VFLASH_ASSERT(vflash_gpu_init(&g,VFLASH_GPU_ZEVIO)==0);
    vflash_gpu_caps_t caps=vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_ZEVIO_TILED);
    VFLASH_ASSERT(caps.color_key_sprite && caps.alpha_blend);
    /* Validate argument and overlap rejections before submitting lists. */
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,-1,8,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,104,-1,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,104,8,-1,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,104,8,16,-1,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,116,8,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,104,20,16,16,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,104,8,16,16,100,12)==VFLASH_GPU_UNSUPPORTED);
    /* Zero-size or completely clipped sprites are no-ops and do not submit lists. */
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,104,8,0,16,8,8)==0);
    VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&a,104,8,16,16,-16,8)==0);
    /* Upload the 16x16 sprite into the offscreen atlas (x=96..127, y=0..31) once per A/B surface. */
    VFLASH_ASSERT(vflash_gpu_upload(&g,&a,&asset,0,0,16,16,104,8)==0);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&b,&asset,0,0,16,16,104,8)==0);
    gpu_transparent_uploads=2;
    uint16_t begin=vflash_clock_ticks();
    for (uint32_t frame=0;frame<32;++frame) {
        vflash_gpu_surface_t *s=(frame&1)?&b:&a;
        uint16_t start=vflash_clock_ticks();
        /* Render multicolor playfield background (x=0..95, y=0..31) without touching atlas x=96..127. */
        fill(&g,s,0,0,96,16,0x102060);
        fill(&g,s,0,16,96,16,0x105020);
        fill(&g,s,0,14,96,4,0xC08020);
        fill(&g,s,30,0,4,32,0x404040);
        fill(&g,s,62,0,4,32,0x404040);
        int32_t sx=(int32_t)((frame*2u)+8u);
        int32_t sy=(int32_t)(1u+((frame*5u)&15u));
        /* Draw unclipped 16x16 transparent sprite (256 pixels -> exercises fast-path tex_setup). */
        VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,s,104,8,16,16,sx,sy)==0);
        /* Draw clipped transparent sprite at left playfield boundary (exercises clip & slow-path texel). */
        VFLASH_ASSERT(vflash_gpu_clip(s,2,4,20,24)==0);
        VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,s,104,8,16,16,-4,8)==0);
        VFLASH_ASSERT(vflash_gpu_clip(s,0,0,128,32)==0);
        VFLASH_ASSERT(vflash_gpu_wait_idle(&g)==0);
        gpu_operation_ticks+=(uint16_t)(vflash_clock_ticks()-start);
        start=vflash_clock_ticks();
        VFLASH_ASSERT(vflash_gpu_present_view(&g,s,96,32,16,24)==0);
        gpu_present_ticks+=(uint16_t)(vflash_clock_ticks()-start);
        /* Reject writes to the displayed buffer, including draw_sprite. */
        VFLASH_ASSERT(vflash_gpu_fill_rect(&g,s,0,0,1,1,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_copy_region(&g,s,104,8,16,16,0,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,s,104,8,16,16,0,0)==VFLASH_GPU_BUSY);
        VFLASH_ASSERT(vflash_gpu_upload(&g,s,&asset,0,0,16,16,104,8)==VFLASH_GPU_BUSY);
        vflash_gpu_surface_t alias=*s;
        alias.fb.pixels+=24576;alias.physical_address+=49152;
        alias.fb.height=8;alias.fb.capacity_pixels=8192;alias.clip_h=8;
        VFLASH_ASSERT(vflash_gpu_draw_sprite(&g,&alias,0,0,8,8,16,0)==VFLASH_GPU_BUSY);
        gpu_transparent_frames=frame+1;
    }
    gpu_total_ticks=(uint16_t)(vflash_clock_ticks()-begin);
    vflash_gpu_stats_t stats;VFLASH_ASSERT(vflash_gpu_stats(&g,&stats)==0);
    gpu_submission_ticks=stats.submission_ticks;gpu_completion_ticks=stats.completion_ticks;gpu_lists=stats.lists;
    VFLASH_ASSERT(vflash_gpu_shutdown(&g)==0);
    VFLASH_ASSERT(REG(0xB8000108)==0 && REG(0xB8000160)==(uint32_t)(uintptr_t)fb.pixels);
    VFLASH_ASSERT(vflash_gpu_wait_idle(&g)==VFLASH_GPU_NOT_INITIALIZED);
    VFLASH_ASSERT(vflash_gpu_init(&g,VFLASH_GPU_ZEVIO)==0);
#if GPU_TRANSPARENT_FINAL == 0
    VFLASH_ASSERT(vflash_gpu_present_view(&g,&a,96,32,0,0)==0);
#elif GPU_TRANSPARENT_FINAL == 1
    VFLASH_ASSERT(vflash_gpu_present_view(&g,&b,96,32,224,208)==0);
#else
    VFLASH_ASSERT(vflash_gpu_shutdown(&g)==0);
#endif
    VFLASH_ASSERT(vflash_panic_vectors_intact());
    render_status=0x44524157;gpu_transparent_status=0x47545331;
    for (;;) __asm__ volatile("nop");
}

