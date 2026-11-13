#include "vflash/clock.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))

//
// Initialize Timer 0 as a 10 kHz free-running count-up clock.
// Pinned ROM handoff: CPU 150 MHz, APB 75 MHz, timer clock 37.5 MHz.
//
// Returns 1 if timer registers match expected configuration, else 0.
//
int vflash_clock_init(void)
{
    if (REG(0x900B0024u) != 0x00000100u)
    {
        return 0;
    }

    REG(0x900A0034u) = 0;     // Timer pair 0 IRQ mask: polling only
    REG(0x90010008u) = 0x10;  // Stop Timer 0
    REG(0x90010004u) = 3749;  // 37.5 MHz / 3750 = 10 kHz
    REG(0x90010000u) = 0;
    REG(0x90010008u) = 0x0F;  // Free running, count up

    return REG(0x90010004u) == 3749 && REG(0x90010008u) == 0x0F;
}

//
// Read the current 16-bit 10 kHz Timer 0 counter value.
//
// Returns the current tick count.
//
uint16_t vflash_clock_ticks(void)
{
    return (uint16_t)REG(0x90010000u);
}
