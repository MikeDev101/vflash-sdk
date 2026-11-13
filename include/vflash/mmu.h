#ifndef VFLASH_MMU_H
#define VFLASH_MMU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Status codes returned by V.Flash MMU operations.
// ARMv5 domain 0, uncached/unbuffered descriptors only. AP controls USER
// permissions; AP=2 does not make privileged code read-only.
//
typedef enum
{
    VFLASH_MMU_OK,
    VFLASH_MMU_ALIGNMENT,
    VFLASH_MMU_CONFLICT,
    VFLASH_MMU_INVALID,
    VFLASH_MMU_BUSY,
    VFLASH_MMU_CAPACITY,
    VFLASH_MMU_UNSUPPORTED
} vflash_mmu_result_t;

//
// ARMv5 page/section access permission constants (AP field).
//
enum
{
    VFLASH_MMU_PRIV_RW = 1,
    VFLASH_MMU_USER_RO = 2,
    VFLASH_MMU_USER_RW = 3
};

//
// Initialize the MMU translation tables in aligned SDRAM.
//
// Returns VFLASH_MMU_OK on success, or VFLASH_MMU_BUSY if already enabled.
//
vflash_mmu_result_t vflash_mmu_init(void);

//
// Populate the initial 1:1 identity map across all SDRAM and I/O regions.
//
// Returns VFLASH_MMU_OK on success, or an error code on failure.
//
vflash_mmu_result_t vflash_mmu_identity(void);

//
// Map a 1 MiB section from virtual address va to physical address pa.
//
// va: 1 MiB-aligned virtual base address.
// pa: 1 MiB-aligned physical base address.
// ap: Access permission constant (VFLASH_MMU_PRIV_RW..USER_RW).
//
// Returns VFLASH_MMU_OK on success, or an alignment/conflict/state error.
//
vflash_mmu_result_t vflash_mmu_map_section(uint32_t va, uint32_t pa,
                                           uint32_t ap);

//
// Map a 4 KiB small page from virtual address va to physical address pa.
//
// va: 4 KiB-aligned virtual base address.
// pa: 4 KiB-aligned physical base address.
// ap: Access permission constant (VFLASH_MMU_PRIV_RW..USER_RW).
//
// Returns VFLASH_MMU_OK on success, or an alignment/capacity/state error.
//
vflash_mmu_result_t vflash_mmu_map_page(uint32_t va, uint32_t pa, uint32_t ap);

//
// Unmap a 4 KiB small page at virtual address va.
//
// va: 4 KiB-aligned virtual base address to unmap.
//
// Returns VFLASH_MMU_OK on success, or an error code if invalid/enabled.
//
vflash_mmu_result_t vflash_mmu_unmap_page(uint32_t va);

//
// Enable the ARM926EJ-S MMU. Requires the identity profile to remain
// intact; tables are immutable while enabled. Call only from privileged ARM
// state with IRQ/FIQ masked, caches off, and no firmware services running.
//
// Returns VFLASH_MMU_OK on success, or an error code on failure.
//
vflash_mmu_result_t vflash_mmu_enable(void);

//
// Disable the ARM926EJ-S MMU and restore flat physical addressing.
//
// Returns VFLASH_MMU_OK on success, or an error code on failure.
//
vflash_mmu_result_t vflash_mmu_disable(void);

//
// Invalidate all entries in the unified TLB.
//
void vflash_mmu_invalidate_tlb(void);

//
// Inspect the raw L1 or L2 page-table descriptor for virtual address va.
//
// va:    Virtual address to look up.
// level: Table level (1 for L1 section/coarse, 2 for L2 small page).
//
// Returns the 32-bit descriptor word, or 0 if unmapped or invalid.
//
uint32_t vflash_mmu_descriptor(uint32_t va, uint32_t level);

//
// Read the CP15 System Control Register (c1).
//
// Returns the current 32-bit CP15 control register value.
//
uint32_t vflash_mmu_control(void);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_MMU_H
