#include "vflash/mmu.h"
#include <stddef.h>
/* Linked only into opt-in programs; BSS storage lies below panic/heap/stack. */
uint32_t vflash_mmu_l1[4096] __attribute__((aligned(16384)));
uint32_t vflash_mmu_l2[4][256] __attribute__((aligned(1024)));
static uint32_t owners[4], used, initialized, active;
extern void vflash_mmu_activate(uint32_t ttbr);
extern void vflash_mmu_deactivate(void);
extern uint32_t vflash_mmu_processor_state(void);
static int valid_ap(uint32_t ap) { return ap >= 1 && ap <= 3; }
static int idle(void) { return !(vflash_mmu_control() & 1); }
vflash_mmu_result_t vflash_mmu_init(void)
{
    if (!idle()) return VFLASH_MMU_BUSY;
    for (uint32_t i=0;i<4096;++i) vflash_mmu_l1[i]=0;
    for (uint32_t i=0;i<4;++i) {
        owners[i]=0;
        for (uint32_t j=0;j<256;++j) vflash_mmu_l2[i][j]=0;
    }
    used=0; initialized=1; active=0;
    return VFLASH_MMU_OK;
}
vflash_mmu_result_t vflash_mmu_map_section(uint32_t va,uint32_t pa,uint32_t ap)
{
    if (!idle()) return VFLASH_MMU_BUSY;
    if (!initialized || !valid_ap(ap)) return VFLASH_MMU_INVALID;
    if ((va|pa)&0xFFFFF) return VFLASH_MMU_ALIGNMENT;
    if (vflash_mmu_l1[va>>20]) return VFLASH_MMU_CONFLICT;
    /* ARM926 section descriptor includes implementation-required bit 4. */
    vflash_mmu_l1[va>>20]=pa|(ap<<10)|0x12;
    return VFLASH_MMU_OK;
}
static int table(uint32_t va)
{
    for (uint32_t i=0;i<used;++i) if (owners[i]==va>>20) return (int)i;
    return -1;
}
vflash_mmu_result_t vflash_mmu_map_page(uint32_t va,uint32_t pa,uint32_t ap)
{
    if (!idle()) return VFLASH_MMU_BUSY;
    if (!initialized || !valid_ap(ap)) return VFLASH_MMU_INVALID;
    if ((va|pa)&4095) return VFLASH_MMU_ALIGNMENT;
    uint32_t index=va>>20;
    if (vflash_mmu_l1[index] && (vflash_mmu_l1[index]&3)!=1) return VFLASH_MMU_CONFLICT;
    int t=table(va);
    if (t<0) {
        if (vflash_mmu_l1[index]) return VFLASH_MMU_CONFLICT;
        if (used==4) return VFLASH_MMU_CAPACITY;
        t=(int)used++; owners[t]=index;
        vflash_mmu_l1[index]=(uint32_t)(uintptr_t)vflash_mmu_l2[t]|0x11;
    }
    uint32_t *entry=&vflash_mmu_l2[t][(va>>12)&255];
    if (*entry) return VFLASH_MMU_CONFLICT;
    /* Replicate AP into each 1 KiB subpage permission field. C=B=0. */
    *entry=pa|(ap*0x550u)|2;
    return VFLASH_MMU_OK;
}
vflash_mmu_result_t vflash_mmu_unmap_page(uint32_t va)
{
    if (!idle()) return VFLASH_MMU_BUSY;
    if (!initialized) return VFLASH_MMU_INVALID;
    if (va&4095) return VFLASH_MMU_ALIGNMENT;
    int t=table(va);
    if (t<0 || !vflash_mmu_l2[t][(va>>12)&255]) return VFLASH_MMU_INVALID;
    vflash_mmu_l2[t][(va>>12)&255]=0;
    return VFLASH_MMU_OK;
}
vflash_mmu_result_t vflash_mmu_identity(void)
{
    vflash_mmu_result_t r=vflash_mmu_init();
    if (r) return r;
    /* Only ROM, all SDRAM, and the two required peripheral section windows. */
    vflash_mmu_map_section(0,0,1); vflash_mmu_map_section(0x100000,0x100000,1);
    for (uint32_t i=0x100;i<0x110;++i) vflash_mmu_map_section(i<<20,i<<20,1);
    vflash_mmu_map_section(0x90000000,0x90000000,1);
    vflash_mmu_map_section(0xB0000000,0xB0000000,1);
    vflash_mmu_map_section(0xB8000000,0xB8000000,1);
    vflash_mmu_map_section(0xC4000000,0xC4000000,1);
    return VFLASH_MMU_OK;
}
uint32_t vflash_mmu_descriptor(uint32_t va,uint32_t level)
{
    if (!initialized) return 0;
    if (level==1) return vflash_mmu_l1[va>>20];
    int t=table(va);
    return level==2 && t>=0 ? vflash_mmu_l2[t][(va>>12)&255] : 0;
}
vflash_mmu_result_t vflash_mmu_enable(void)
{
    if (!idle()) return VFLASH_MMU_BUSY;
    uint32_t state=vflash_mmu_processor_state(), c=vflash_mmu_control();
    if (!initialized || (state&0xFF)!=0xD3 || (c&0x3004)) return VFLASH_MMU_UNSUPPORTED;
    for (uint32_t i=0;i<4096;++i) {
        int required=i<2 || (i>=0x100 && i<0x110) || i==0x900 || i==0xB00 || i==0xB80 || i==0xC40;
        if (required && vflash_mmu_l1[i]!=((i<<20)|0x412u)) return VFLASH_MMU_UNSUPPORTED;
    }
    vflash_mmu_activate((uint32_t)(uintptr_t)vflash_mmu_l1);
    active=vflash_mmu_control()&1;
    return active ? VFLASH_MMU_OK : VFLASH_MMU_UNSUPPORTED;
}
vflash_mmu_result_t vflash_mmu_disable(void)
{
    /* Only identity-profile activation is exposed; nonidentity execution is
     * deliberately unsupported across the transition. */
    if ((vflash_mmu_processor_state()&0xFF)!=0xD3 || (vflash_mmu_control()&0x1004))
        return VFLASH_MMU_UNSUPPORTED;
    if (!active) return idle() ? VFLASH_MMU_OK : VFLASH_MMU_UNSUPPORTED;
    vflash_mmu_deactivate();
    active=vflash_mmu_control()&1;
    return active ? VFLASH_MMU_UNSUPPORTED : VFLASH_MMU_OK;
}
