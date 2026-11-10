.syntax unified
.cpu arm926ej-s
.section .text.panic_test_trigger,"ax",%progbits
.align 2
.global panic_test_trigger
.type panic_test_trigger,%function
#if PANIC_TEST_MODE == 5 || PANIC_TEST_MODE == 9
.thumb
.thumb_func
panic_test_trigger:
#if PANIC_TEST_MODE == 9
.global panic_fault_instruction
panic_fault_instruction:
    swi 0
    b .
#else
    .hword 0xDE00
    nop /* pinned interpreter incorrectly skips this instead of taking UND */
    ldr r0, =thumb_probe_returned
    movs r1, #1
    str r1, [r0]
    bx lr
#endif
#else
.arm
panic_test_trigger:
#if PANIC_TEST_MODE == 1 || PANIC_TEST_MODE == 6 || PANIC_TEST_MODE == 7 || PANIC_TEST_MODE == 8
#if PANIC_TEST_MODE == 7
    msr cpsr_c, #0xdf /* SYS shares the USR SP/LR bank */
    ldr sp, =0x10fff800
    ldr lr, =0x12345678
#elif PANIC_TEST_MODE == 8
    mov sp, #0
#endif
    ldr r0, =0x11111111
    ldr r1, =0x22222222
    ldr r2, =0x33333333
    ldr r3, =0x44444444
    ldr r4, =0x55555555
    ldr r5, =0x66666666
    ldr r6, =0x77777777
    ldr r7, =0x88888888
    ldr r8, =0x99999999
    ldr r9, =0xAAAAAAAA
    ldr r10, =0xBBBBBBBB
    ldr r11, =0xDDDDDDDD
    ldr r12, =0xCCCCCCCC
    cmp r0, r0 /* known N/Z/C/V = 0/1/1/0 */
.global panic_fault_instruction
panic_fault_instruction:
    .word 0xE7F000F0
    b .
#elif PANIC_TEST_MODE == 4
.global panic_fault_instruction
panic_fault_instruction:
    swi 0
    b .
#else
#if PANIC_TEST_MODE == 2
    mov sp, #0
#endif
    mov r0, #1
    ldr r1, =panic_test_message
    b vflash_panic
#endif
#endif
.ltorg
.section .rodata.panic_test_message,"a",%progbits
panic_test_message: .asciz "DELIBERATE SDK DIAGNOSTIC"
.section .note.GNU-stack,"",%progbits
