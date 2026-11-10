#include "vflash/gpu.h"
#include "vflash/video.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#ifdef GPU_FRAME_MMU
#include "vflash/mmu.h"
#endif
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint16_t frame_a[16384] __attribute__((aligned(64)));
volatile uint16_t frame_b[16384] __attribute__((aligned(64)));
volatile uint32_t gpu_frame_status,gpu_frame_count,gpu_operation_ticks,gpu_present_ticks,gpu_total_ticks,gpu_frame_control;
volatile uint32_t gpu_submission_ticks,gpu_completion_ticks,gpu_lists;
volatile uint16_t frame_wide[24576] __attribute__((aligned(64)));
volatile uint16_t sprite_linear[128] __attribute__((aligned(64)));
static void fill(vflash_gpu_t *g,vflash_gpu_surface_t *s,int x,int y,int w,int h,uint32_t c)
{ VFLASH_ASSERT(vflash_gpu_fill_rect(g,s,x,y,w,h,c)==0); }
void vflash_main(void)
{
    startup_status=boot_marker==0 && data_cookie==0x13579BDF?0x600D600D:0;boot_marker=0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef GPU_FRAME_MMU
    VFLASH_ASSERT(vflash_mmu_identity()==0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000,0xA8000000,1)==0);
    VFLASH_ASSERT(vflash_mmu_enable()==0);
#endif
    uint32_t c;__asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(c));gpu_frame_control=c;
    for (uint32_t i=0;i<16384;++i) frame_a[i]=frame_b[i]=0x255A;
    for (uint32_t i=0;i<24576;++i) frame_wide[i]=0x255A;
    for (uint32_t i=0;i<128;++i) sprite_linear[i]=(uint16_t)(0x4000u+i);
    vflash_framebuffer_t fb;VFLASH_ASSERT(vflash_video_init(&fb));video_status=0x56494431;
    draw_clear(&fb,0);VFLASH_ASSERT(vflash_video_present(&fb));
    vflash_gpu_t g;vflash_gpu_surface_t a,b,wide,sprite;
    vflash_framebuffer_t sprite_fb={(uint16_t *)sprite_linear,16,8,16,128,VFLASH_BGR555};
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled(&a,(void *)frame_a,(uint32_t)(uintptr_t)frame_a,sizeof(frame_a))==0);
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled(&b,(void *)frame_b,(uint32_t)(uintptr_t)frame_b,sizeof(frame_b))==0);
    VFLASH_ASSERT(vflash_gpu_surface_register_tiled_ex(&wide,(void *)frame_wide,(uint32_t)(uintptr_t)frame_wide,96,24,sizeof(frame_wide))==0);
    VFLASH_ASSERT(vflash_gpu_surface_register(&sprite,&sprite_fb,(uint32_t)(uintptr_t)sprite_linear,1)==0);
    VFLASH_ASSERT(vflash_gpu_init(&g,VFLASH_GPU_ZEVIO)==0);
    vflash_gpu_caps_t caps=vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_ZEVIO_TILED);
    VFLASH_ASSERT(caps.hardware_accelerated && caps.fill_rect && caps.present && caps.same_surface_copy && caps.upload && !caps.blit);
    VFLASH_ASSERT(!vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_LINEAR).fill_rect);
    VFLASH_ASSERT(vflash_gpu_init(&g,VFLASH_GPU_SOFTWARE)==VFLASH_GPU_BUSY);
    VFLASH_ASSERT(vflash_gpu_blit(&g,&a,&b,0,0)==VFLASH_GPU_UNSUPPORTED);
    VFLASH_ASSERT(vflash_gpu_copy_region(&g,&a,0,0,16,8,1,0)==VFLASH_GPU_UNSUPPORTED);
    VFLASH_ASSERT(vflash_gpu_present(&g,&a,-1,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_present(&g,&a,257,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_present(&g,&wide,225,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_present(&g,&wide,0,217)==VFLASH_GPU_INVALID);
    vflash_gpu_surface_t linear;
    VFLASH_ASSERT(vflash_gpu_surface_register(&linear,&fb,0,0)==0);
    VFLASH_ASSERT(vflash_gpu_fill_rect(&g,&linear,0,0,1,1,0)==VFLASH_GPU_UNSUPPORTED);
    VFLASH_ASSERT(vflash_gpu_copy_region(&g,&linear,0,0,1,1,2,2)==VFLASH_GPU_UNSUPPORTED);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&a,&linear,0,0,16,8,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&wide,&sprite,0,0,15,8,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&wide,&sprite,1,0,14,8,0,0)==VFLASH_GPU_INVALID);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&wide,&sprite,2,0,14,8,0,0)==VFLASH_GPU_UNSUPPORTED);
    fill(&g,&wide,0,0,96,24,0);
    fill(&g,&wide,64,16,32,8,0x00FFFF);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&wide,&sprite,2,0,14,1,0,23)==0);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&wide,&sprite,0,0,16,8,40,8)==0);
    VFLASH_ASSERT(vflash_gpu_copy_region(&g,&wide,40,8,16,8,72,16)==0);
    VFLASH_ASSERT(vflash_gpu_present(&g,&wide,224,216)==0);
    VFLASH_ASSERT(vflash_gpu_upload(&g,&wide,&sprite,0,0,16,8,0,0)==VFLASH_GPU_BUSY);
    uint16_t begin=vflash_clock_ticks();
    for (uint32_t frame=0;frame<32;++frame) {
        vflash_gpu_surface_t *s=(frame&1)?&b:&a;
        uint16_t start=vflash_clock_ticks();
        fill(&g,s,-17,-9,128,64,0);
        if (!(frame&1)) {
            fill(&g,s,0,0,32,8,0xFF0000);fill(&g,s,32,0,32,8,0x00FF00);
            fill(&g,s,0,8,32,8,0x0000FF);fill(&g,s,32,8,32,8,0xFFFFFF);
            fill(&g,s,28,6,12,4,0xFFFF00);
            VFLASH_ASSERT(vflash_gpu_clip(s,7,3,5,6)==0);
            fill(&g,s,-1,-1,100,100,0xFF00FF);
            VFLASH_ASSERT(vflash_gpu_copy_region(&g,s,0,0,16,4,44,12)==VFLASH_GPU_UNSUPPORTED);
            VFLASH_ASSERT(vflash_gpu_clip(s,0,0,64,16)==0);
        } else {
            fill(&g,s,0,0,64,16,0x00FF00);fill(&g,s,31,7,2,2,0xFF0000);
            fill(&g,s,0,0,16,4,0xFFFFFF);
        }
        VFLASH_ASSERT(vflash_gpu_copy_region(&g,s,0,0,16,4,44,12)==0);
        VFLASH_ASSERT(vflash_gpu_wait_idle(&g)==0);
        gpu_operation_ticks+=(uint16_t)(vflash_clock_ticks()-start);
        start=vflash_clock_ticks();VFLASH_ASSERT(vflash_gpu_present(&g,s,13,19)==0);
        gpu_present_ticks+=(uint16_t)(vflash_clock_ticks()-start);
        VFLASH_ASSERT(vflash_gpu_fill_rect(&g,s,0,0,1,1,0)==VFLASH_GPU_BUSY);
        vflash_gpu_surface_t alias=*s;
        alias.fb.pixels+=32;alias.physical_address+=64;
        VFLASH_ASSERT(vflash_gpu_fill_rect(&g,&alias,0,0,1,1,0)==VFLASH_GPU_BUSY);
        gpu_frame_count=frame+1;
    }
    gpu_total_ticks=(uint16_t)(vflash_clock_ticks()-begin);
    vflash_gpu_stats_t stats;VFLASH_ASSERT(vflash_gpu_stats(&g,&stats)==0);
    gpu_submission_ticks=stats.submission_ticks;gpu_completion_ticks=stats.completion_ticks;gpu_lists=stats.lists;
    VFLASH_ASSERT(vflash_gpu_shutdown(&g)==0);
    VFLASH_ASSERT(REG(0xB8000108)==0 && REG(0xB8000160)==(uint32_t)(uintptr_t)fb.pixels);
    VFLASH_ASSERT(vflash_gpu_wait_idle(&g)==VFLASH_GPU_NOT_INITIALIZED);
    VFLASH_ASSERT(vflash_gpu_init(&g,VFLASH_GPU_ZEVIO)==0);
#if GPU_FRAME_FINAL == 0
    VFLASH_ASSERT(vflash_gpu_present(&g,&a,0,0)==0);
#elif GPU_FRAME_FINAL == 1
    VFLASH_ASSERT(vflash_gpu_present(&g,&b,256,224)==0);
#else
    VFLASH_ASSERT(vflash_gpu_shutdown(&g)==0);
#endif
    render_status=0x44524157;gpu_frame_status=0x47465231;
    for (;;) __asm__ volatile("nop");
}
