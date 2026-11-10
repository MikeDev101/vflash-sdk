#ifndef VFLASH_PANIC_H
#define VFLASH_PANIC_H
#include <stdint.h>

enum { VFLASH_PANIC_FATAL = 1, VFLASH_PANIC_ASSERT = 2, VFLASH_PANIC_UNDEFINED = 3,
       VFLASH_PANIC_SWI = 4, VFLASH_PANIC_PREFETCH_ABORT = 5, VFLASH_PANIC_DATA_ABORT = 6 };
enum { VFLASH_VECTOR_UNDEFINED = 1, VFLASH_VECTOR_SWI = 2 };
enum { VFLASH_CRASH_HARDWARE = 1, VFLASH_CRASH_SP_VALID = 2, VFLASH_CRASH_LR_VALID = 4,
       VFLASH_CRASH_FAULT_REGS_VALID = 8, VFLASH_CRASH_PC_VALID = 16 };
typedef struct {
    uint32_t magic, version, reason, flags, fault_pc, cpsr, spsr;
    uint32_t exception_lr, entry_sp, origin_sp, origin_lr, registers[13];
    uint32_t fault_address, fault_status, secondary_reason, secondary_pc, vector_mask;
    char message[64];
} vflash_crash_record_t;
typedef struct { uint32_t vector, slot, original, patched, active; } vflash_vector_backup_t;
extern volatile vflash_crash_record_t vflash_crash_record;
extern volatile vflash_vector_backup_t vflash_vector_backups[2];
void vflash_panic_init(void);
uint32_t vflash_panic_install_vectors(uint32_t mask);
int vflash_panic_restore_vectors(void);
int vflash_panic_vectors_intact(void);
__attribute__((noreturn)) void vflash_panic(uint32_t reason, const char *message);
void vflash_panic_draw(uint16_t *pixels, const vflash_crash_record_t *record);
#define VFLASH_ASSERT(expression) do { if (!(expression)) vflash_panic(VFLASH_PANIC_ASSERT, #expression); } while (0)
#endif
