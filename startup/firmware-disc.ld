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
    .data : ALIGN(4) { *(.data .data.*) } > RAM :data
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM :data
    __image_end = .;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    __stack_bottom = __stack_top - 16K;
    ASSERT(__image_end <= __stack_bottom, "Probe overlaps its 16 KiB stack")
    /DISCARD/ : { *(.comment) *(.note*) }
}
