#ifndef VFLASH_MMU_H
#define VFLASH_MMU_H
#include <stdint.h>
/* ARMv5 domain 0, uncached/unbuffered descriptors only. AP controls USER
 * permissions: AP=2 does not make privileged code read-only. */
typedef enum { VFLASH_MMU_OK, VFLASH_MMU_ALIGNMENT, VFLASH_MMU_CONFLICT,
    VFLASH_MMU_INVALID, VFLASH_MMU_BUSY, VFLASH_MMU_CAPACITY, VFLASH_MMU_UNSUPPORTED } vflash_mmu_result_t;
enum { VFLASH_MMU_PRIV_RW = 1, VFLASH_MMU_USER_RO = 2, VFLASH_MMU_USER_RW = 3 };
vflash_mmu_result_t vflash_mmu_init(void);
vflash_mmu_result_t vflash_mmu_identity(void);
vflash_mmu_result_t vflash_mmu_map_section(uint32_t va, uint32_t pa, uint32_t ap);
vflash_mmu_result_t vflash_mmu_map_page(uint32_t va, uint32_t pa, uint32_t ap);
vflash_mmu_result_t vflash_mmu_unmap_page(uint32_t va);
/* Activation requires the original complete identity profile to remain intact.
 * Tables are immutable while enabled. Only call from privileged ARM state with
 * IRQ/FIQ masked, caches off, and no firmware services running. */
vflash_mmu_result_t vflash_mmu_enable(void);
vflash_mmu_result_t vflash_mmu_disable(void);
void vflash_mmu_invalidate_tlb(void);
uint32_t vflash_mmu_descriptor(uint32_t va, uint32_t level);
uint32_t vflash_mmu_control(void);
#endif
