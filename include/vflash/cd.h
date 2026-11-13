#ifndef VFLASH_CD_H
#define VFLASH_CD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Size in bytes of a single Mode 1 CD-ROM user data sector.
//
#define VFLASH_CD_SECTOR_SIZE 2048

//
// Initialize the optical CD-ROM hardware controller, configure the CD DSP
// clock, clear status interrupts, and prime head tracking.
//
// Returns 0 on success, or a negative error code on failure.
//
int vflash_cd_init(void);

//
// Read a single 2048-byte Mode 1 data sector by Logical Block Address.
//
// lba: Logical Block Address of the sector to read.
// dst: Destination buffer of at least VFLASH_CD_SECTOR_SIZE bytes.
//
// Returns 1 on success, or a negative error code on failure.
//
int vflash_cd_read_sector(uint32_t lba, void *dst);

//
// Read sequential 2048-byte Mode 1 sectors starting at lba.
//
// lba:   Starting Logical Block Address.
// count: Number of consecutive sectors to read.
// dst:   Destination buffer of at least count * VFLASH_CD_SECTOR_SIZE bytes.
//
// Returns the number of sectors read on success, or a negative error code.
//
int vflash_cd_read_sectors(uint32_t lba, uint32_t count, void *dst);

//
// Query the last tracked optical disc head Logical Block Address.
//
// Returns the current tracked LBA.
//
uint32_t vflash_cd_get_current_lba(void);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_CD_H
