#ifndef VFLASH_PANIC_H
#define VFLASH_PANIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Panic and hardware exception reason codes.
//
enum
{
    VFLASH_PANIC_FATAL          = 1,
    VFLASH_PANIC_ASSERT         = 2,
    VFLASH_PANIC_UNDEFINED      = 3,
    VFLASH_PANIC_SWI            = 4,
    VFLASH_PANIC_PREFETCH_ABORT = 5,
    VFLASH_PANIC_DATA_ABORT     = 6
};

//
// Bitmask flags for CPU exception vector installation.
//
enum
{
    VFLASH_VECTOR_UNDEFINED = 1,
    VFLASH_VECTOR_SWI       = 2
};

//
// Bitmask flags describing valid fields in vflash_crash_record_t.
//
enum
{
    VFLASH_CRASH_HARDWARE         = 1,
    VFLASH_CRASH_SP_VALID         = 2,
    VFLASH_CRASH_LR_VALID         = 4,
    VFLASH_CRASH_FAULT_REGS_VALID = 8,
    VFLASH_CRASH_PC_VALID         = 16
};

//
// Captured CPU registers, fault status, and message for a fatal panic.
//
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t reason;
    uint32_t flags;
    uint32_t fault_pc;
    uint32_t cpsr;
    uint32_t spsr;
    uint32_t exception_lr;
    uint32_t entry_sp;
    uint32_t origin_sp;
    uint32_t origin_lr;
    uint32_t registers[13];
    uint32_t fault_address;
    uint32_t fault_status;
    uint32_t secondary_reason;
    uint32_t secondary_pc;
    uint32_t vector_mask;
    char message[64];
} vflash_crash_record_t;

//
// Saved original and patched instruction words for an exception vector slot.
//
typedef struct
{
    uint32_t vector;
    uint32_t slot;
    uint32_t original;
    uint32_t patched;
    uint32_t active;
} vflash_vector_backup_t;

extern volatile vflash_crash_record_t vflash_crash_record;
extern volatile vflash_vector_backup_t vflash_vector_backups[2];

//
// Clear crash record state and initialize the panic subsystem.
//
void vflash_panic_init(void);

//
// Install ARM exception vector hooks for the requested exception mask.
//
// mask: Bitmask of VFLASH_VECTOR_* flags to install.
//
// Returns the bitmask of vectors successfully installed.
//
uint32_t vflash_panic_install_vectors(uint32_t mask);

//
// Restore previously patched ARM exception vectors to their original words.
//
// Returns 1 on success, or 0 if restoration fails.
//
int vflash_panic_restore_vectors(void);

//
// Verify that installed exception vectors still contain the expected patches.
//
// Returns 1 if all active vectors are intact, or 0 otherwise.
//
int vflash_panic_vectors_intact(void);

//
// Record a fatal panic reason and message, render the crash screen, and halt.
//
// reason:  Panic reason code (VFLASH_PANIC_*).
// message: Optional null-terminated diagnostic string.
//
__attribute__((noreturn)) void vflash_panic(uint32_t reason,
                                            const char *message);

//
// Render a crash record onto a 320x240 BGR555 pixel buffer.
//
// pixels: Destination 320x240 BGR555 pixel array.
// record: Crash record to format and display.
//
void vflash_panic_draw(uint16_t *pixels, const vflash_crash_record_t *record);

#ifdef __cplusplus
}
#endif

//
// Evaluate expression and trigger VFLASH_PANIC_ASSERT if false.
//
#define VFLASH_ASSERT(expression) \
    do { if (!(expression)) vflash_panic(VFLASH_PANIC_ASSERT, #expression); } \
    while (0)

#endif // VFLASH_PANIC_H
