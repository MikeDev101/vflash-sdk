#include "vflash/gpu.h"
#include "vflash/video.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#ifdef VFLASH_GPU_MMU
#include "vflash/mmu.h"
#endif
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint32_t gpu_status,gpu_clear_ticks,gpu_fill_ticks,gpu_blit_ticks;
static uint16_t sprite[64] __attribute__((aligned(64)));
void vflash_main(void)
{
    startup_status=boot_marker==0 && data_cookie==0x13579BDF ? 0x600D600D:0;
    boot_marker=0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef VFLASH_GPU_MMU
    VFLASH_ASSERT(vflash_mmu_identity()==0 && vflash_mmu_enable()==0);
#endif
    vflash_framebuffer_t fb,small={sprite,8,8,8,64,VFLASH_BGR555};
    VFLASH_ASSERT(vflash_video_init(&fb));video_status=0x56494431;
    vflash_gpu_t g;vflash_gpu_surface_t d,s;
    VFLASH_ASSERT(vflash_gpu_init(&g,VFLASH_GPU_SOFTWARE)==0);
    VFLASH_ASSERT(vflash_gpu_surface_register(&d,&fb,0,0)==0);
    VFLASH_ASSERT(vflash_gpu_surface_register(&s,&small,0,0)==0);
    uint16_t start=vflash_clock_ticks();
    for (uint32_t i=0;i<8;++i) VFLASH_ASSERT(vflash_gpu_fill_rect(&g,&d,0,0,320,240,0)==0);
    gpu_clear_ticks=(uint16_t)(vflash_clock_ticks()-start);
    start=vflash_clock_ticks();
    for (uint32_t i=0;i<8;++i) VFLASH_ASSERT(vflash_gpu_fill_rect(&g,&d,10,10,100,60,0xFF0000)==0);
    gpu_fill_ticks=(uint16_t)(vflash_clock_ticks()-start);
    VFLASH_ASSERT(vflash_gpu_fill_rect(&g,&s,0,0,8,8,0x00FF00)==0);
    start=vflash_clock_ticks();
    for (uint32_t i=0;i<8;++i) VFLASH_ASSERT(vflash_gpu_blit(&g,&d,&s,60,40)==0);
    gpu_blit_ticks=(uint16_t)(vflash_clock_ticks()-start);
    VFLASH_ASSERT(vflash_gpu_clip(&d,20,20,40,40)==0);
    VFLASH_ASSERT(vflash_gpu_fill_rect(&g,&d,-1,-1,320,240,0x0000FF)==0);
    VFLASH_ASSERT(vflash_gpu_wait_idle(&g)==0);
    render_status=vflash_video_present(&fb)?0x44524157:0;
    gpu_status=0x47505531;
    for (;;) __asm__ volatile("nop");
}
