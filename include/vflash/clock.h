#ifndef VFLASH_CLOCK_H
#define VFLASH_CLOCK_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 10 kHz count-up clock; subtract in uint16_t across the 6.5536 s wrap. */
int vflash_clock_init(void);
uint16_t vflash_clock_ticks(void);

#ifdef __cplusplus
}
#endif

#endif
