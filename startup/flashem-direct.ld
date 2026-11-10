/* Direct-load experiment ONLY: source evidence and allocation rationale in
 * docs/hardware.md. This is not a verified uMORE application layout. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0x10C00000, LENGTH = 4M }
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
