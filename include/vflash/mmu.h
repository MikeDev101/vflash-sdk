#ifndef VFLASH_MMU_H
#define VFLASH_MMU_H

#include <stddef.h>
#include <stdint.h>

#define VFLASH_CACHE_LINE_SIZE 32u

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
// Map a 1 MiB section with explicit cacheability and bufferability bits.
//
// va:         Virtual base address (1 MiB aligned).
// pa:         Physical base address (1 MiB aligned).
// ap:         Access permission (VFLASH_MMU_PRIV_RW, USER_RO, or USER_RW).
// cacheable:  Non-zero to set the C bit (1) in the L1 section descriptor.
// bufferable: Non-zero to set the B bit (1) in the L1 section descriptor.
//
// Returns VFLASH_MMU_OK on success, or an error code on failure.
//
vflash_mmu_result_t vflash_mmu_map_section_ex(uint32_t va, uint32_t pa,
                                              uint32_t ap, uint32_t cacheable,
                                              uint32_t bufferable);

//
// Enable the ARM926EJ-S MMU, I-cache, write-back D-cache, and write buffer.
// Requires the identity profile (including uncached MMIO windows) to remain
// intact; tables are immutable while enabled. Call only from privileged ARM
// state with IRQ/FIQ masked and no firmware services running.
//
// Returns VFLASH_MMU_OK on success, or an error code on failure.
//
vflash_mmu_result_t vflash_mmu_enable(void);

//
// Clean/invalidate D-cache, disable the ARM926EJ-S MMU and caches, and
// restore flat physical addressing.
//
// Returns VFLASH_MMU_OK on success, or an error code on failure.
//
vflash_mmu_result_t vflash_mmu_disable(void);

//
// Clean dirty D-cache lines covering [addr, addr + length) to SDRAM and drain
// the write buffer. Use before handing a CPU-written buffer to hardware DMA.
//
// addr:   Start address of the memory range.
// length: Length of the memory range in bytes.
//
#if defined(__arm__)
//
// Clean dirty D-cache lines covering [addr, addr + length) to SDRAM and drain
// the write buffer. Use before handing a CPU-written buffer to hardware DMA.
//
// addr:   Start address of the memory range.
// length: Length of the memory range in bytes.
//
void vflash_dcache_clean_range(const void *addr, size_t length);

//
// Invalidate D-cache lines covering [addr, addr + length) and drain the write
// buffer. Unaligned boundary cache lines are cleaned before invalidation so
// adjacent data is preserved. Use after hardware DMA writes to SDRAM.
//
// addr:   Start address of the memory range.
// length: Length of the memory range in bytes.
//
void vflash_dcache_invalidate_range(void *addr, size_t length);

//
// Clean dirty D-cache lines and invalidate D-cache lines covering
// [addr, addr + length), then drain the write buffer. Use before starting
// device-to-memory DMA on a buffer that may hold stale dirty cache lines.
//
// addr:   Start address of the memory range.
// length: Length of the memory range in bytes.
//
void vflash_dcache_clean_invalidate_range(void *addr, size_t length);

//
// Invalidate the entire ARM926EJ-S instruction cache and flush the prefetch
// buffer.
//
void vflash_icache_invalidate_all(void);
#else
static inline void vflash_dcache_clean_range(const void *addr, size_t length)
{
    (void)addr;
    (void)length;
}
static inline void vflash_dcache_invalidate_range(void *addr, size_t length)
{
    (void)addr;
    (void)length;
}
static inline void vflash_dcache_clean_invalidate_range(void *addr, size_t length)
{
    (void)addr;
    (void)length;
}
static inline void vflash_icache_invalidate_all(void) {}
#endif

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
