/* Tested loader: A29L160ATV ROM SHA-256 467744d11dcf011762f41db4be60efbbd
 * 63cd2420003a1de41c421b4c88533ba. BOOT header/stub occupy the first 24 bytes.
 * The firmware disables CP15 control and enters SVC with IRQ/FIQ masked.
 * This payload takes over execution; it does not return to uMORE. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0x10C00018, LENGTH = 0x003FFFE8 }
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS
{
    .text : ALIGN(4) { KEEP(*(.text.startup)) *(.text .text.*) } > RAM :code
    .rodata : ALIGN(4) { *(.rodata .rodata.*) } > RAM :code
    .preinit_array : ALIGN(4) {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > RAM :code
    .init_array : ALIGN(4) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array*))
        KEEP(*(.ctors*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > RAM :code
    .fini_array : ALIGN(4) {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array*))
        KEEP(*(.dtors*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > RAM :code
    .data : ALIGN(4) { *(.data .data.*) } > RAM :data
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM :data
    .panic (NOLOAD) : ALIGN(64) {
        __panic_start = .;
        KEEP(*(.panic.record)) KEEP(*(.panic.vectors)) KEEP(*(.panic.state))
        KEEP(*(.panic.stack)) KEEP(*(.panic.framebuffer))
        . = ALIGN(64);
        __panic_end = .;
    } > RAM :data
    __image_end = .;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    __stack_bottom = __stack_top - 16K;
    __heap_start = ALIGN(__image_end, 64);
    __heap_end = __stack_bottom;
    ASSERT(__heap_start <= __heap_end, "Probe overlaps its 16 KiB stack")
    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
}
