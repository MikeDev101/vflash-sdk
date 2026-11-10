# Panic and exception handling

This is an emulator-tested takeover facility for the pinned FlashEm interpreter,
not an exception framework verified on a physical V.Flash. The SDK installs the
undefined-instruction hook during startup when the inspected ROM vector profile
and takeover CPU state match; otherwise it leaves that hook unchanged.
Deliberate faults are confined to separate diagnostic executables; normal
startup does not execute a fault.

## Try it

```sh
./play-homebrew.sh --demo panic
./play-homebrew.sh --demo panic --panic-test manual
```

The first command builds and boots a separate diagnostic through the original
firmware, then executes ARM `.word 0xE7F000F0`. A blue screen reports the exception,
PC, CPSR, SPSR, interrupted SP and exception LR. It stays halted until you close
FlashEm. `--emulator`, `--bios` and `--scale` work as with the other demos.

Application code can include `vflash/panic.h` and call:

```c
#include "vflash/panic.h"

vflash_panic(VFLASH_PANIC_FATAL, "Framebuffer initialization failed");
VFLASH_ASSERT(framebuffer != 0);
```

Manual panic records the caller's return address, not an exact faulting PC.
It uses neither the application's stack nor the heap on entry. Do not pass an
arbitrary invalid message pointer: the handler bounds reads to ROM/SDRAM and
63 printable characters, but cannot establish that an arbitrary address contains
a valid message. The handler never resumes execution.

## What is actually validated

| Facility | FlashEm validation |
| --- | --- |
| Manual panic/assert API | Record and direct panic scanout testable |
| ARM undefined instruction | Genuine CPU trap; exact UDF PC and saved registers checked |
| ARM SWI/SVC | Genuine CPU trap in opt-in diagnostic; firmware SWI remains installed by default |
| Thumb undefined instruction | Pinned interpreter misdecodes `0xDE00`; negative probe returns and reports manual panic |
| Thumb SWI | CPU trap and SPSR T bit captured; fault PC marked unknown because interpreter LR semantics differ |
| Data abort | Current memory bus does not deliver CPU data-abort exceptions; no hook installed |
| Prefetch abort | Current memory bus does not deliver CPU prefetch-abort exceptions; no hook installed |
| IRQ/FIQ | Not hooked; separate investigation required; takeover keeps them masked |

There are no synthetic abort demonstrations presented as hardware validation.
Null reads are not a useful fault test: address zero contains mapped firmware.
Fault-address/status fields are zero and their validity flag is clear.

## Vectors, ownership and restoration

For the tested ROM SHA-256
`467744d11dcf011762f41db4be60efbbd63cd2420003a1de41c421b4c88533ba`,
low vectors are read-only ROM. Their `LDR pc` instructions point at RAM code:

| ROM vector | RAM instruction | Original word | Literal containing firmware destination |
| --- | --- | --- | --- |
| UND `0x04` | `0x1000FF84` | `0xE59FF040` | `0x1000FFCC` |
| SWI `0x08` | `0x1000FF88` | `0xE59FF040` | `0x1000FFD0` |

The SDK decodes the ROM vector, checks the writable slot and original instruction
shape, saves the exact word, and replaces that one RAM instruction with a
range-checked ARM branch to its handler. It changes neither ROM nor the original
firmware destination literals. It refuses installation with high vectors, MMU,
or caches enabled. `vflash_vector_backups` records original/patched words,
addresses and active flags. `vflash_panic_vectors_intact()` checks active patches.

`vflash_panic_restore_vectors()` verifies ownership, restores active original
words, and performs instruction synchronization. If another writer changed a
patch, restoration fails without replacing that writer's instruction. It is an
explicit privileged operation under the same masked-interrupt, cache-off takeover
conditions as installation. Restoration does not resume the old firmware or
reconstruct its other device/task state. `restore` verifies readback before
reinstalling UND and taking a genuine trap.

Default ownership is UND only. SWI is left untouched; existing firmware SWI
services have **not** been characterized sufficiently to claim that replacing
that hook is safe for applications calling them. Only the `swi` and `thumb-swi`
diagnostic executables opt in via `vflash_panic_install_vectors(VFLASH_VECTOR_SWI)`.

The firmware initialization writes these RAM slots, so they are not generally
unused RAM. This SDK takes over after the disc loader, never returns to the old
scheduler, and keeps IRQ/FIQ masked. The acceptance test watches both slots from
reset and verifies that after takeover the only writes are SDK patch/restore
operations. Its normal timer/video initialization also checks that the hooks
remain intact. This establishes ownership for the tested takeover path, not for
future firmware callbacks, interrupt handlers, DMA workloads or other ROMs.
Reassess ownership before enabling those services.

## Context and instruction state

Assembly saves R0-R12, entry SP, exception LR, CPSR and SPSR before C handling.
Condition flags are saved before comparisons. It switches to the dedicated panic
stack without using the interrupted SP, sets the entry guard before building the
frame, masks IRQ/FIQ, then enters C. Exception entries are ARM code.

For UND interrupting SVC, the handler briefly selects the SVC bank to copy its
SP/LR, without reading/writing that bank's stack. It also handles the shared
USR/SYS SP/LR bank. `system` checks known SYS SP/LR values. When SWI originates
in SVC, exception entry already overwrote SVC LR: the record preserves raw
exception LR and entry SP, but marks the original LR invalid. Validity means
that a value was captured, not that the saved pointer is safe to dereference.
Other interrupted modes are not promised complete banked-register capture;
SP/LR validity stays clear. FIQ's separate R8-R12 bank is outside this
facility's supported origins.

For the validated ARM UND/SWI path, fault PC is exception LR minus four. For
Thumb, the record retains raw SPSR/exception LR, sets fault PC to zero and leaves
`PC_VALID` clear. It never applies the ARM subtraction to Thumb. ARM's documented
Thumb SWI/UND convention uses the following halfword address (fault PC LR minus
two), but the pinned emulator's shared exception entry computes LR using its
ARM-oriented PC convention. The Thumb SWI diagnostic exercises this discrepancy.
See Arm's [Developer Guide, section 5.11](https://documentation-service.arm.com/static/5e9578fe8259fe2368e2c274).

## Reserved storage and nested faults

Both linker scripts reserve a 64-byte-aligned `.panic` NOLOAD region after
application BSS and before `__heap_start`. It contains the 180-byte record,
vector backups, entry guard, 4096-byte emergency stack and separate 153600-byte
320x240 framebuffer. Linker assertions keep the region and heap below the normal
16 KiB stack. Normal framebuffers are in BSS; future allocators must use
`[__heap_start, __heap_end)`. Panic rendering submits no DMA and does not use the
normal drawing/presentation, timing or double-buffer machinery. Future DMA
buffers must obey the same linker allocation boundaries.

The version-1 record symbol is `vflash_crash_record`; addresses vary with each
ELF. Its magic `0x56435253` is committed after the primary record is populated.
The exported header defines the field order and validity flags. A subsequent
fault takes a stack-free assembly path, writes only `secondary_reason` and
`secondary_pc`, and loops. It does not call C or attempt another render. The
`nested` test faults after rendering and verifies the primary record/screen are
preserved. A failure earlier in rendering may leave an incomplete screen.

Persistence means SDRAM remains inspectable while halted and in FlashEm's RAM
dump. It does not mean power-cycle or reset persistence: startup invalidates the
old record. Nothing is automatically written to a virtual card.

## Build one diagnostic

Each script builds its diagnostic ELF, BOOT.BIN and ISO/BIN/CUE disc images.
Run it from any directory; it configures CMake on the first build and prints the
ELF and CUE paths on success. These scripts only build; use the panic launcher
above to open the result in FlashEm. No ROM is required to build.

| Script | Diagnostic |
| --- | --- |
| `./scripts/panic/build-manual.sh` | Manual fatal panic |
| `./scripts/panic/build-undefined.sh` | ARM undefined-instruction trap |
| `./scripts/panic/build-stack.sh` | Manual panic with SP zero |
| `./scripts/panic/build-nested.sh` | Secondary fault during panic handling |
| `./scripts/panic/build-swi.sh` | Opt-in ARM SWI trap |
| `./scripts/panic/build-thumb.sh` | Thumb UDF negative capability probe |
| `./scripts/panic/build-restore.sh` | Restore/readback/reinstall, then ARM UDF |
| `./scripts/panic/build-system.sh` | SYS banked SP/LR capture through ARM UDF |
| `./scripts/panic/build-stack-undefined.sh` | ARM UDF with SP zero |
| `./scripts/panic/build-thumb-swi.sh` | Opt-in Thumb SWI capture |

For example, `./scripts/panic/build-undefined.sh --parallel 4` forwards the extra options
to `cmake --build`. Outputs are under `build/panic/TEST/` and the matching ELF is
`build/vflash_panic_TEST.elf`. All wrappers use `scripts/build-panic-test.sh`.

## Reproduce acceptance

```sh
cmake --build build --target panic_manual_disc_image panic_undefined_disc_image \
  panic_stack_disc_image panic_nested_disc_image panic_swi_disc_image \
  panic_thumb_disc_image panic_restore_disc_image panic_system_disc_image \
  panic_stack-undefined_disc_image panic_thumb-swi_disc_image
python3 tools/panic_smoke.py \
  --emulator /path/to/flashem --bios '/path/to/A29L160ATV@TSOP48.BIN' \
  --test all --out build/panic-acceptance
```

The runner uses actual firmware disc boot without `--load`, checks the existing
boot markers, reads the ELF symbols and complete SDRAM dump, validates context
and reserved ranges, audits trampoline writes, compares all native panic pixels
against the emergency C renderer compiled on the host, and compares the actual
FlashEm RGB screenshot. Host rendering supplies reference pixels only; it never
injects anything into guest RAM. Each case produces `panic-result.json`,
`crash-record.bin`, `frame.ppm`, `ram.bin`, `run.log` and the underlying boot
`result.json` with ROM/emulator/ELF hashes. Success evidence is removed before a
new run. `--test undefined` selects one case; building its image remains explicit.

`stack` branches into manual panic with SP zero; `stack-undefined` takes UND
with SP zero. `nested` and those bad-stack probes are diagnostic-only builds.
Host tests check renderer bounds, record ABI and register display changes.
They cannot establish exception-vector delivery.

Source audit: pinned FlashEm
[`arm9.c`](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/arm9.c)
(`arm9_undef`, `arm9_swi`, `exec_thumb`) and
[`hw.c`](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/hw.c)
(memory bus, vector-write watch and display engine). The ROM itself remains a
private local fixture and is not included in this repository.

## Historical validation

The [panic milestone report](../history/panic-milestone.md) preserves the first
register capture and ownership observations. Those addresses and test counts
are build-specific. The current runner always resolves addresses from its ELF.
