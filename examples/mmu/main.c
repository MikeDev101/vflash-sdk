#include "vflash/mmu.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/video.h"
#include "demo.h"
volatile uint32_t boot_marker,startup_status,video_status,render_status;
volatile uint32_t data_cookie=0x13579BDF;
volatile uint32_t mmu_status,mmu_enabled_control,mmu_disabled_control,mmu_init_ticks,mmu_tables_bytes;
volatile uint32_t mmu_probe __attribute__((aligned(4096)));
volatile uint32_t mmu_probe_second __attribute__((aligned(4096)));
void vflash_main(void)
{
    startup_status=boot_marker==0 && data_cookie==0x13579BDF ? 0x600D600D:0;
    boot_marker=0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
    uint16_t start=vflash_clock_ticks();
    VFLASH_ASSERT(vflash_mmu_identity()==0);
    VFLASH_ASSERT(vflash_mmu_map_page(0x20000000,(uint32_t)(uintptr_t)&mmu_probe,1)==0);
    mmu_init_ticks=(uint16_t)(vflash_clock_ticks()-start);
    mmu_tables_bytes=20480;
    VFLASH_ASSERT(vflash_mmu_enable()==0);
    mmu_enabled_control=vflash_mmu_control();
    *(volatile uint32_t *)0x20000000=0x1926ABCD;
    VFLASH_ASSERT(mmu_probe==0x1926ABCD);
    VFLASH_ASSERT(vflash_mmu_unmap_page(0x20000000)==VFLASH_MMU_BUSY);
    vflash_framebuffer_t fb;
    VFLASH_ASSERT(vflash_video_init(&fb));video_status=0x56494431;
    demo_render(&fb);VFLASH_ASSERT(vflash_video_present(&fb));render_status=0x44524157;
    VFLASH_ASSERT(vflash_mmu_disable()==0);
    mmu_disabled_control=vflash_mmu_control();
    VFLASH_ASSERT(vflash_mmu_unmap_page(0x20000000)==0);
    mmu_probe_second=0xC001CAFE;
    VFLASH_ASSERT(vflash_mmu_map_page(0x20000000,(uint32_t)(uintptr_t)&mmu_probe_second,1)==0);
    VFLASH_ASSERT(vflash_mmu_enable()==0);
    VFLASH_ASSERT(*(volatile uint32_t *)0x20000000==0xC001CAFE);
    mmu_status=0x4D4D5531;
#ifdef VFLASH_MMU_UDF
    __asm__ volatile(".global mmu_fault_instruction\nmmu_fault_instruction: .word 0xE7F000F0");
#endif
    for (;;) __asm__ volatile("nop");
}
