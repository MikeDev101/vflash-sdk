#include "vflash/clock.h"
#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))

int vflash_clock_init(void)
{
    /* Pinned ROM handoff: CPU 150 MHz, APB 75 MHz, timer clock 37.5 MHz.
     * hw.c pmu_set_clocks, timer_read/write/advance: divider counts +1. */
    if (REG(0x900B0024u) != 0x00000100u) return 0;
    REG(0x900A0034u) = 0;      /* timer pair 0 IRQ mask: polling only */
    REG(0x90010008u) = 0x10;  /* stop timer 0 */
    REG(0x90010004u) = 3749;  /* 37.5 MHz / 3750 = 10 kHz */
    REG(0x90010000u) = 0;
    REG(0x90010008u) = 0x0F;  /* free running, count up */
    return REG(0x90010004u) == 3749 && REG(0x90010008u) == 0x0F;
}

uint16_t vflash_clock_ticks(void)
{
    return (uint16_t)REG(0x90010000u);
}
