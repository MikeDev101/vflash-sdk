.syntax unified
.cpu arm926ej-s
.arm
/* Raw entry frame is 80 bytes. No instruction uses the interrupted stack.
 * The PC-relative scratch words live in writable application SDRAM with
 * caches off. Their addresses are never used as exception vectors. */
.macro PANIC_ENTRY name, reason, hardware
.section .text.\name, "ax", %progbits
.align 2
.global \name
.type \name, %function
\name:
    str r0, 8f
    str r1, 10f
    mrs r0, cpsr
    str r0, 11f
    str sp, 9f
    ldr r0, =vflash_panic_busy
    ldr r0, [r0]
    cmp r0, #0
    bne 7f
    mov r1, #1
    ldr r0, =vflash_panic_busy
    str r1, [r0]
    ldr r0, 8f
    ldr r1, 10f
    ldr sp, =vflash_panic_stack + 4096
    sub sp, sp, #80
    stmia sp, {r0-r12, lr}
    ldr r2, 9f
    str r2, [sp, #56]
    ldr r2, 11f
    str r2, [sp, #60]
    orr r3, r2, #0xc0
    msr cpsr_c, r3
.if \hardware
    mrs r3, spsr
    str r3, [sp, #64]
    mov r4, #1
    mov r5, #0
    str r5, [sp, #68]
    str r5, [sp, #72]
    and r5, r3, #0x1f
    and r6, r2, #0x1f
    cmp r5, r6
    beq 2f
    cmp r5, #0x13
    beq 1f
    cmp r5, #0x10
    moveq r5, #0x1f
    cmp r5, #0x1f
    bne 3f
1:  orr r5, r5, #0xc0
    msr cpsr_c, r5
    mov r5, sp
    mov r6, lr
    orr r2, r2, #0xc0
    msr cpsr_c, r2
    str r5, [sp, #68]
    str r6, [sp, #72]
    orr r4, r4, #6
    b 3f
2:  ldr r5, [sp, #56]
    str r5, [sp, #68]
    mov r5, #0
    str r5, [sp, #72]
    orr r4, r4, #2
3:  str r4, [sp, #76]
    mov r0, #\reason
.else
    mov r3, #0
    str r3, [sp, #64]
    ldr r3, [sp, #56]
    str r3, [sp, #68]
    str lr, [sp, #72]
    mov r3, #6
    str r3, [sp, #76]
    ldr r0, [sp]
.endif
    mov r1, sp
    bl vflash_panic_dispatch
6:  b 6b
7:  ldr r0, =vflash_crash_record
.if \hardware
    mov r1, #\reason
.else
    ldr r1, 8f
.endif
    str r1, [r0, #104]
    mov r2, lr
.if \hardware
    mrs r3, spsr
    tst r3, #32
    subeq r2, r2, #4
    movne r2, #0
.endif
    str r2, [r0, #108]
    b 6b
.align 2
8:  .word 0
9:  .word 0
10: .word 0
11: .word 0
.ltorg
.size \name, . - \name
.endm
PANIC_ENTRY vflash_panic, 1, 0
PANIC_ENTRY vflash_undefined_entry, 3, 1
PANIC_ENTRY vflash_swi_entry, 4, 1
.section .note.GNU-stack, "", %progbits
