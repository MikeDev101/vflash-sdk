#ifndef VFLASH_CD_H
#define VFLASH_CD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VFLASH_CD_SECTOR_SIZE 2048

/*
 * Initialize the optical CD hardware controller.
 * Configures CD DSP clock, clears status interrupts, and primes head tracking.
 * Returns 0 on success, negative error code on failure.
 */
int vflash_cd_init(void);

/*
 * Read a single 2048-byte Mode 1 data sector by Logical Block Address (LBA).
 * Returns 1 on success, negative error code on failure.
 */
int vflash_cd_read_sector(uint32_t lba, void *dst);

/*
 * Read multiple sequential 2048-byte sectors starting from LBA.
 * Returns number of sectors read on success, negative error code on failure.
 */
int vflash_cd_read_sectors(uint32_t lba, uint32_t count, void *dst);

/*
 * Get the last tracked disc head LBA.
 */
uint32_t vflash_cd_get_current_lba(void);

#ifdef __cplusplus
}
#endif

#endif /* VFLASH_CD_H */

