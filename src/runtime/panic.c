#include "vflash/panic.h"
#include <stddef.h>

#define RESERVED(name) __attribute__((section(name), aligned(64)))
volatile vflash_crash_record_t vflash_crash_record RESERVED(".panic.record");
volatile vflash_vector_backup_t vflash_vector_backups[2] RESERVED(".panic.vectors");
volatile uint32_t vflash_panic_busy RESERVED(".panic.state");
uint8_t vflash_panic_stack[4096] RESERVED(".panic.stack");
uint16_t vflash_panic_pixels[320 * 240] RESERVED(".panic.framebuffer");
static uint32_t vector_mask;
extern void vflash_undefined_entry(void), vflash_swi_entry(void);
_Static_assert(offsetof(vflash_crash_record_t, secondary_reason) == 104, "assembly crash offsets");
_Static_assert(sizeof(vflash_crash_record_t) == 180, "crash format version 1");

static void instruction_barrier(void)
{
    uint32_t zero = 0;
    __asm__ volatile("mcr p15,0,%0,c7,c10,4\n mcr p15,0,%0,c7,c5,0\n mcr p15,0,%0,c7,c5,4" :: "r"(zero) : "memory");
}

int vflash_panic_vectors_intact(void)
{
    for (uint32_t i = 0; i < 2; ++i)
        if (vflash_vector_backups[i].active &&
            *(volatile uint32_t *)(uintptr_t)vflash_vector_backups[i].slot != vflash_vector_backups[i].patched) return 0;
    return 1;
}

static uint32_t vector_slot(uint32_t vector)
{
    uint32_t instruction = *(volatile const uint32_t *)(uintptr_t)vector;
    if ((instruction & 0xFF7FF000u) != 0xE51FF000u) return 0;
    int32_t literal = (int32_t)vector + 8 + ((instruction & (1u << 23)) ?
        (int32_t)(instruction & 4095) : -(int32_t)(instruction & 4095));
    if (literal < 0 || literal >= 0x200000 || (literal & 3)) return 0;
    uint32_t slot = *(volatile const uint32_t *)(uintptr_t)(uint32_t)literal;
    return slot >= 0x1000FF80u && slot < 0x10010000u && !(slot & 3) ? slot : 0;
}

uint32_t vflash_panic_install_vectors(uint32_t mask)
{
    uint32_t control;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(control));
    if (control & ((1u << 13) | 1u | 4u | (1u << 12))) return vector_mask;
    for (uint32_t i = 0; i < 2; ++i) {
        if (!(mask & (1u << i)) || vflash_vector_backups[i].active) continue;
        uint32_t vector = 4 + 4 * i;
        uint32_t slot = vector_slot(vector);
        if (!slot || (*(volatile uint32_t *)(uintptr_t)slot & 0xFF7FF000u) != 0xE51FF000u) continue;
        uint32_t target = (uint32_t)(uintptr_t)(i ? vflash_swi_entry : vflash_undefined_entry);
        int32_t distance = (int32_t)(target - slot - 8);
        if ((distance & 3) || distance < -33554432 || distance > 33554428) continue;
        volatile vflash_vector_backup_t *backup = &vflash_vector_backups[i];
        backup->vector = vector;
        backup->slot = slot;
        backup->original = *(volatile uint32_t *)(uintptr_t)slot;
        backup->patched = 0xEA000000u | (((uint32_t)distance >> 2) & 0xFFFFFFu);
        *(volatile uint32_t *)(uintptr_t)slot = backup->patched;
        backup->active = 1;
        vector_mask |= 1u << i;
    }
    instruction_barrier();
    return vector_mask;
}

int vflash_panic_restore_vectors(void)
{
    if (!vflash_panic_vectors_intact()) return 0;
    for (uint32_t i = 0; i < 2; ++i) {
        if (vflash_vector_backups[i].active) {
            *(volatile uint32_t *)(uintptr_t)vflash_vector_backups[i].slot = vflash_vector_backups[i].original;
            vflash_vector_backups[i].active = 0;
        }
    }
    vector_mask = 0;
    instruction_barrier();
    return 1;
}

void vflash_panic_init(void)
{
    vflash_panic_busy = 0;
    vflash_crash_record.magic = 0;
    vflash_crash_record.version = 1;
    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t slot = vector_slot(4 + 4 * i);
        vflash_vector_backups[i].vector = 4 + 4 * i;
        vflash_vector_backups[i].slot = slot;
        vflash_vector_backups[i].original = slot ? *(volatile const uint32_t *)(uintptr_t)slot : 0;
        vflash_vector_backups[i].patched = 0;
        vflash_vector_backups[i].active = 0;
    }
    vector_mask = 0;
    vflash_panic_install_vectors(VFLASH_VECTOR_UNDEFINED);
}

static void show_panic(void)
{
    vflash_panic_draw(vflash_panic_pixels, (const vflash_crash_record_t *)&vflash_crash_record);
    volatile uint32_t *ve = (volatile uint32_t *)(uintptr_t)0xB8000000u;
    ve[0x100 / 4] = 0;
    ve[0x004 / 4] = 0;
    ve[0x00C / 4] = 0x00EF013F;
    ve[0x108 / 4] = 0;
    ve[0x130 / 4] = 0;
    ve[0x134 / 4] = 0x00EF013F;
    ve[0x160 / 4] = (uint32_t)(uintptr_t)vflash_panic_pixels;
    __asm__ volatile("" ::: "memory");
    ve[0x100 / 4] = 0x10;
}

__attribute__((noreturn)) void vflash_panic_dispatch(uint32_t reason, const uint32_t *raw)
{
    vflash_panic_busy = 1;
    volatile vflash_crash_record_t *record = &vflash_crash_record;
    record->magic = 0;
    record->version = 1;
    record->reason = reason;
    record->flags = raw[19];
    record->exception_lr = raw[13];
    record->entry_sp = raw[14];
    record->cpsr = raw[15];
    record->spsr = raw[16];
    record->origin_sp = raw[17];
    record->origin_lr = raw[18];
    record->fault_pc = raw[13] & ~1u;
    if (record->flags & VFLASH_CRASH_HARDWARE) {
        // Only ARM-state reconstruction is validated. The pinned interpreter
        // misdecodes Thumb UDF and has different Thumb LR semantics.
        if (!(record->spsr & 32)) { record->fault_pc -= 4; record->flags |= VFLASH_CRASH_PC_VALID; }
        else record->fault_pc = 0;
    }
    for (uint32_t i = 0; i < 13; ++i) record->registers[i] = raw[i];
    record->fault_address = record->fault_status = 0; // abort bus is not implemented
    record->secondary_reason = record->secondary_pc = 0;
    record->vector_mask = vector_mask;
    for (uint32_t i = 0; i < 64; ++i) record->message[i] = 0;
    if (!(record->flags & VFLASH_CRASH_HARDWARE)) {
        uintptr_t address = raw[1];
        for (uint32_t i = 0; i < 63; ++i) {
            uintptr_t p = address + i;
            if (!((p >= 0x10000000u && p < 0x11000000u) || p < 0x200000u)) break;
            char c = *(const char *)p;
            if (!c) break;
            record->message[i] = c >= 32 && c <= 126 ? c : '?';
        }
    }
    record->magic = 0x56435253u;
    show_panic();
#ifdef VFLASH_PANIC_INJECT_NESTED
    __asm__ volatile(".global panic_nested_fault\npanic_nested_fault: .word 0xE7F000F0");
#endif
    for (;;) __asm__ volatile("nop");
}
