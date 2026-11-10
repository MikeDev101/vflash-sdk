# CPU, memory and takeover contract

Current display implementation and emulator-only evaluation scope are in
[video.md](video.md). The framebuffer is linker-owned `.bss`, not a borrowed
firmware allocation. Physical hardware access is outside the current scope.

Evidence is pinned to FlashEm commit
`fb109dd01a49239c5884fcec60b9dc451fb651f8`. An emulator implementation is useful
reverse-engineering evidence; it is not a hardware datasheet or a guarantee of
physical-console behavior.

| Item | Evidence and status |
| --- | --- |
| ARM926EJ-S / 150 MHz / ZEVIO 1020 | Reported by the emulator's [README](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/README.md); not independently measured here |
| 16 MiB SDRAM at `0x10000000..0x10FFFFFF` | Implemented by `RAM_BASE`, `RAM_SIZE`, `hw_load_phys` in [hw.c](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/hw.c); used by the tested direct loader |
| Boot ROM | The locally supplied 2 MiB ROM identified in [firmware.md](../research/firmware.md) booted our generated disc in FlashEm |
| On-chip memory | The model has 2 KiB palette RAM at `0xB8000800` and an 8 KiB mailbox at `0xF8000000`. No general-purpose internal SRAM allocation is established |
| OS | The local ROM's UART banner reports µMORE v4.0 SDK ARM9T; the general application ABI remains unverified |
| Optical media | Emulator implements ISO payload and raw 2352-byte-sector BIN/CUE reads; guest EDC/ECC checks require raw data |

The [GCC ARM options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html) define
`-mcpu` instruction selection and `-mfloat-abi=soft` software floating point.
Our ELF attributes report `ARM926EJ-S`, `v5TEJ`, ARM ISA, and EABI5 soft float.
ARM926EJ-S includes the Jazelle extension to ARMv5TE; the sample emits ordinary
ARM instructions. It does not use hardware floating point or Thumb-2.

## Direct-load layout (`flashem-direct.ld`)

[vflash_load_program](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/vflash.c)
loads ELF `PT_LOAD` segments at their physical addresses, zeroes the remainder
of each memory span, and starts at `e_entry`. It sets ARM SVC mode with IRQ/FIQ
masked, MMU and caches off. The firmware has not executed.

The linker uses `0x10C00000` because upstream's
[boot observations](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/CLAUDE.md)
identify that base in an observed commercial BOOT header, and the implemented
SDRAM map covers it. For this isolated cold-load experiment we allocate the
upper 4 MiB of modeled SDRAM. This allocation is our experimental choice; it
does not establish an available 4 MiB firmware application arena.

The descending, 8-byte-aligned stack begins at the end of SDRAM (`0x11000000`),
with the final 16 KiB reserved by a linker assertion. The first stack store
would be below the top. No original ROM, OS, disc DMA, or interrupt handler is
running during this test. The banked stacks initialized by FlashEm are not used.
Firmware-managed tasks cannot assume ownership of this stack; the separately
verified takeover path is described below.

Both `.data` addresses are identical (VMA = LMA); the direct loader places its
initial bytes in RAM, so startup performs no ROM-to-RAM data copy. Startup sets
SP, clears word-aligned `.bss`, calls `vflash_panic_init`, then enters
`vflash_main`. The panic installer validates the ROM vector profile and CPU
state before patching UND. A synthetic direct-load ROM does not match that
profile, so its vectors remain untouched. Manual panic is still available.
Startup does not initialize µMORE or resume its services. See the
[panic reference](panic.md) for the hook and reserved-memory contract.
The sample stores its marker in its own linker-allocated variable rather than
an assumed globally safe firmware mailbox.

The direct-load RAM test observes zero-initialized state and preserved `.data`. Since
FlashEm's ELF loader also zeroes `.bss`, that test alone cannot isolate the
startup zeroing loop; the generated disassembly exposes that loop for review.

## Tested firmware entry model

The supplied ROM's loader was subsequently inspected and traced. It disables
MMU/caches through CP15 control and selects SVC with IRQ/FIQ masked before
branching to the header's `load + entry_offset`. `firmware-disc.ld` reserves
the first 24 bytes at `0x10C00000` for the generated BOOT header and entry stub,
and links the payload at `0x10C00018`. It uses the same upper-SDRAM stack region.
The firmware's active loader stack was observed at `0x101D09C0`, below the
payload. Our code takes over the machine with interrupts masked and never
returns to the firmware, calls its services, or resumes the old scheduler.

The complete disc path now launches the payload and preserves the RAM marker
in FlashEm. Its startup clears `.bss` without assistance from an ELF loader.
This validates the minimal allocation on the tested emulator/ROM combination;
it does not establish universal free RAM ranges or hardware compatibility.
See [firmware.md](../research/firmware.md) for the static and dynamic evidence.

## Requirements for a general firmware application model

Before supporting a firmware-managed task or a new ROM, establish the actual
copied range and entry address, permitted RAM and stack ownership, CPU mode, MMU
mappings, cache/coherency requirements, interrupt state, task/scheduler state,
return contract, relocation behavior, and whether `.data`/`.bss` initialization
belongs to the loader or the payload. The existing takeover script verifies
one specific ROM/emulator path; its contract does not establish those properties for firmware-managed tasks.

## Reserved SDK regions

Both linker scripts allocate normal code/data/BSS, then a dedicated `.panic`
NOLOAD region, then expose `[__heap_start, __heap_end)` below the reserved
16 KiB application stack. No heap allocator exists yet. Normal and panic
framebuffers are separate linker-owned allocations. The panic region contains
its crash record, vector backups, entry guard, emergency stack and scanout
buffer. Use ELF symbols for addresses; do not copy milestone addresses into
application code or DMA descriptors.
