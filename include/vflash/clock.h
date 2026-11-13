#ifndef VFLASH_CLOCK_H
#define VFLASH_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Initialize the 10 kHz hardware count-up clock (Timer 0).
//
// Returns 1 on success, or 0 if timer configuration fails.
//
int vflash_clock_init(void);

//
// Read the current 10 kHz free-running tick counter.
// Subtract in uint16_t across the 6.5536-second 16-bit wrap.
//
// Returns the current 16-bit tick count.
//
uint16_t vflash_clock_ticks(void);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_CLOCK_H
