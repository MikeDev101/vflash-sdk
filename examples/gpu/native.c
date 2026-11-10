/* Isolated model-driven experiment, NOT the Zevio HAL backend. */
#include "vflash/video.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint16_t gpu_tiled[8192] __attribute__((aligned(64)));
static uint32_t list[]={0x96000000,0x82000000,0x8300701F,
    0xD3000003,0,0x00080020,0x001F,0x1400FD01};
volatile uint32_t gpu_status,gpu_submit_ticks,gpu_wait_ticks,gpu_completion;
void vflash_main(void)
{
    startup_status=boot_marker==0 && data_cookie==0x13579BDF?0x600D600D:0;boot_marker=0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
    for (uint32_t i=0;i<8192;++i) gpu_tiled[i]=0x255A;
    /* All CPU writes to the tiled destination finish before submission. */
    __asm__ volatile("" ::: "memory");
    REG(0xA8000014)=0x3F;
    REG(0xA8000090)=512;
    REG(0xA8000098)=(uint32_t)(uintptr_t)gpu_tiled;
    REG(0xA8000028)=(uint32_t)(uintptr_t)list;
    uint16_t start=vflash_clock_ticks();REG(0xA8000000)=1;
    gpu_submit_ticks=(uint16_t)(vflash_clock_ticks()-start);
    start=vflash_clock_ticks();
    while (!(REG(0xA8000014)&8)) VFLASH_ASSERT((uint16_t)(vflash_clock_ticks()-start)<1000);
    gpu_wait_ticks=(uint16_t)(vflash_clock_ticks()-start);
    gpu_completion=REG(0xA8000014);REG(0xA8000014)=0x3F;
    for (uint32_t i=0;i<8192;++i) VFLASH_ASSERT(gpu_tiled[i]==(i<256?0x001F:0x255A));
    vflash_framebuffer_t fb;VFLASH_ASSERT(vflash_video_init(&fb));video_status=0x56494431;
    draw_clear(&fb,0);
    /* Explicit CPU copy for linear presentation; no CPU fill of gpu_tiled. */
    for (uint32_t y=0;y<8;++y) for (uint32_t x=0;x<32;++x) fb.pixels[y*320+x]=gpu_tiled[y*32+x];
    render_status=vflash_video_present(&fb)?0x44524157:0;
    gpu_status=0x47505532;
    for (;;) __asm__ volatile("nop");
}
