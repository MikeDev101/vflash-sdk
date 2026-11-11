/* FlashEm --load and the inspected ROM's disc handoff both enter ARM SVC
 * with IRQ/FIQ masked and MMU/caches off. See docs/firmware.md. This is a
 * takeover payload, not a callable uMORE task and it never returns. */
.syntax unified
.cpu arm926ej-s
.arm
.section .text.startup, "ax", %progbits
.global _start
.type _start, %function
_start:
    ldr sp, =__stack_top
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    bl vflash_panic_init

    /* Call constructors in .preinit_array */
    ldr r4, =__preinit_array_start
    ldr r5, =__preinit_array_end
2:
    cmp r4, r5
    bhs 3f
    ldr r3, [r4], #4
    cmp r3, #0
    blxne r3
    b 2b
3:
    /* Call constructors in .init_array */
    ldr r4, =__init_array_start
    ldr r5, =__init_array_end
4:
    cmp r4, r5
    bhs 5f
    ldr r3, [r4], #4
    cmp r3, #0
    blxne r3
    b 4b
5:
    bl vflash_main
6:
    b 6b
.size _start, . - _start
.section .note.GNU-stack, "", %progbits
