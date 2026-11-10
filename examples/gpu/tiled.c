/* Experimental native tiled path: no CPU writes to either surface after submit. */
#include "vflash/video.h"
#include "vflash/draw.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#ifdef GPU_TILED_MMU
#include "vflash/mmu.h"
#endif
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint16_t tiled_a[16384] __attribute__((aligned(64)));
volatile uint16_t tiled_b[16384] __attribute__((aligned(64)));
volatile uint16_t tiled_readback[1024] __attribute__((aligned(64)));
volatile uint32_t tiled_status,tiled_completions,tiled_swaps,tiled_control;
static uint32_t list[96] __attribute__((aligned(64)));
static uint32_t count;
static void fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t c)
{
    list[count++]=0xD3000003;list[count++]=y<<16|x;
    list[count++]=h<<16|w;list[count++]=c;
}
static void begin(void)
{
    count=0;list[count++]=0x96000000;list[count++]=0x82000000;
    list[count++]=0x8300FFFF; /* clip 0,0 through 4095,15 */
}
static void run(volatile uint16_t *surface)
{
    list[count++]=0x1400FD01;
    REG(0xA8000014)=0x3F;
    REG(0xA8000090)=512;REG(0xA8000098)=(uint32_t)(uintptr_t)surface;
    REG(0xA8000028)=(uint32_t)(uintptr_t)list;
    __asm__ volatile("" ::: "memory");REG(0xA8000000)=1;
    uint16_t start=vflash_clock_ticks();
    while (!(REG(0xA8000014)&8)) VFLASH_ASSERT((uint16_t)(vflash_clock_ticks()-start)<1000);
    /* Completion condition only: high bits are not a hardware signature. */
    REG(0xA8000014)=0x3F;VFLASH_ASSERT(!(REG(0xA8000014)&0x3F));
    ++tiled_completions;
}
static void settle(void)
{
    for (uint32_t i=0;i<20;++i) {
        REG(0xB8000000)=2;uint16_t start=vflash_clock_ticks();
        while (!(REG(0xB8000000)&2)) VFLASH_ASSERT((uint16_t)(vflash_clock_ticks()-start)<1000);
    }
}
static void tiled_present(volatile uint16_t *surface)
{
    REG(0xB8000100)=0;REG(0xB8000108)=4;
    REG(0xB8000130)=19u<<16|13u;REG(0xB8000134)=34u<<16|76u;
    REG(0xB8000160)=(uint32_t)(uintptr_t)surface;
    __asm__ volatile("" ::: "memory");REG(0xB8000100)=16;
    VFLASH_ASSERT(REG(0xB800000C)==0x00EF013F && REG(0xB8000004)==0);
    ++tiled_swaps;settle();
}
void vflash_main(void)
{
    startup_status=boot_marker==0 && data_cookie==0x13579BDF?0x600D600D:0;boot_marker=0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef GPU_TILED_MMU
    VFLASH_ASSERT(vflash_mmu_identity()==0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000,0xA8000000,1)==0);
    VFLASH_ASSERT(vflash_mmu_enable()==0);
#endif
    uint32_t control;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(control));
    tiled_control=control;
    for (uint32_t i=0;i<16384;++i) tiled_a[i]=tiled_b[i]=0x255A;
    begin();fill(0,0,32,8,31);fill(32,0,32,8,0x3E0);
    fill(0,8,32,8,0x7C00);fill(32,8,32,8,0x7FFF);
    fill(28,6,12,4,0x3FF);fill(7,3,5,6,0x7C1F);run(tiled_a);
    begin();fill(0,0,64,16,0x7C00);fill(31,7,2,2,31);run(tiled_b);
    /* D2 has ONE backing surface, not independent src/dst base registers. */
    begin();list[count++]=0xD2000003;list[count++]=0;list[count++]=16u<<16|64u;list[count++]=80;
    run(tiled_a);
    /* Overlap probe, modeled snapshot semantics, deliberately isolated. */
    begin();list[count++]=0xD2000003;list[count++]=80;list[count++]=14u<<16|60u;list[count++]=2u<<16|84u;
    run(tiled_a);
    begin();list[count++]=0x0C000000;list[count++]=(uint32_t)(uintptr_t)tiled_readback;
    list[count++]=0xD1000002;list[count++]=80;list[count++]=16u<<16|64u;
    run(tiled_a);
    vflash_framebuffer_t fb;VFLASH_ASSERT(vflash_video_init(&fb));video_status=0x56494431;
    draw_clear(&fb,0);VFLASH_ASSERT(vflash_video_present(&fb));
    tiled_present(tiled_a);tiled_present(tiled_b);
    /* Recovery uses the existing driver, not a second linear implementation. */
    VFLASH_ASSERT(vflash_video_init(&fb));VFLASH_ASSERT(vflash_video_present(&fb));settle();
    VFLASH_ASSERT(REG(0xB8000108)==0 && REG(0xB8000160)==(uint32_t)(uintptr_t)fb.pixels);
#if GPU_TILED_FINAL == 0
    tiled_present(tiled_a);
#elif GPU_TILED_FINAL == 1
    tiled_present(tiled_b);
#endif
    VFLASH_ASSERT(vflash_panic_vectors_intact());
    tiled_status=0x54494C31;render_status=0x44524157;
    for (;;) __asm__ volatile("nop");
}
