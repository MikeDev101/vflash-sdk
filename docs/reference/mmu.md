# Optional ARM926 translation bring-up

This is a partial milestone: translation infrastructure is implemented and
firmware-booted; memory protection and genuine memory aborts are not validated.
No normal SDK target links this driver or enables the MMU. There is no SDL2 or
threading implementation to modify. Existing BOOT/BIN/CUE formats are unchanged.

## Emulator audit

Audited FlashEm interpreter revision `fb109dd01a49239c5884fcec60b9dc451fb651f8`:

| Feature | Status in active CPU/bus path |
| --- | --- |
| CP15 c1, TTBR c2 | Implemented; toggling MMU/TTBR invalidates model translations |
| DACR c3 | Stored, but domains are not enforced by `hw.c:walk` |
| Sections, coarse/fine tables, small pages | Address translation implemented; 4 KiB alias tested here |
| AP/subpage permissions | Missing enforcement |
| TLB | Partial: direct-mapped software cache, generation invalidation; page-table writes auto-flush, unlike real hardware |
| Translation failure | Logs and returns failed bus lookup; reads return zero and writes are dropped |
| Data/prefetch abort | Missing CPU entry from bus/fetch failure; not equivalent to stopping or UND |
| DFSR/DFAR | Stored registers, not populated by memory fault delivery |
| IFSR | Incorrect c5 selection by CRm rather than opcode_2 |
| Cache/write buffer | Cache operations largely no-op; c1 incorrectly forces bit 3 high on readback, though input write-buffer state is separate |

`cp15_translate` is a separate incomplete helper; the active walker is in
`src/hw.c`, including host-pointer fast paths. The CPU also caches instruction
fetch pointers. A correct emulator patch needs access-type/mode-aware checks
on fast and slow paths, deferred architectural exception delivery without
committing failed instruction results, and separate instruction/data fault
register updates. DMA must remain physical. No emulator changes were made;
no emulator-specific workaround is embedded in this SDK driver.

## Driver and memory map

Public API: `include/vflash/mmu.h`; implementation: `src/platform/mmu.c`,
`src/platform/mmu_arm926.S`, and `src/platform/cache_arm926.S`. Static BSS tables
comprise one 16384-byte-aligned L1 (16384 bytes)
and four 1024-byte-aligned coarse L2 tables (4096 bytes), plus 28 bytes of state
and linker alignment padding. The diagnostic also has a page-aligned probe.
All storage is linker-owned below panic storage, heap symbols and main stack.
No allocation occurs. No memory protection is claimed for the tables themselves.

Identity sections cover these windows with cache attributes matched to hardware
DMA and MMIO rules:

| Address interval, end exclusive | Attributes | Use |
| --- | --- | --- |
| 0x00000000–0x00200000 | C=1, B=0 | ROM/low vectors |
| 0x10000000–0x11000000 | C=1, B=1 | SDRAM: firmware trampolines, code, tables, framebuffers, panic and stacks |
| 0x90000000–0x90100000 | C=0, B=0 | Timer/PMU/UART0 peripheral window |
| 0xA8000000–0xA8100000 | C=0, B=0 | Zevio 2D/3D GPU peripheral window |
| 0xB0000000–0xB0100000 | C=0, B=0 | Audio peripheral window |
| 0xB8000000–0xB8100000 | C=0, B=0 | Video peripheral window |
| 0xC4000000–0xC4100000 | C=0, B=0 | CD-ROM peripheral window |

All mappings use domain 0. DACR is 1: domain 0 client, other domains
no access. Sections use `(PA & ~0xfffff) | AP<<10 | C<<3 | B<<2 | 0x12`; coarse descriptors
use aligned table PA plus 0x11; small pages use PA plus AP*0x550 plus 2,
replicating AP in all four 1 KiB subpages. AP=1 is privileged RW/user denied;
AP=2 is privileged RW/user RO, not privileged RO; AP=3 permits both RW.
`vflash_mmu_map_section_ex` permits explicit `C`/`B` selection (default `0, 0` via
`vflash_mmu_map_section`), and re-mapping an existing section with an identical
descriptor returns `VFLASH_MMU_OK`.

Mappings can be edited only with MMU off. Alignment, AP values, conflicting
mappings and four-table capacity are checked. Existing sections are never
silently split or overwritten. Unmapped page slots remain fault descriptors;
this is not tested fault delivery or a usable guarded-stack allocator yet.
Initialization deterministically clears all tables. A coarse table is not
reclaimed by unmap until the next init.

Activation accepts only ARM SVC with IRQ/FIQ masked, low vectors, and the complete
required identity profile intact (with MMIO windows uncached/unbuffered).
Assembly drains the write buffer, loads TTBR, sets client DACR, clears FCSE PID,
invalidates unified TLB and I/D caches, sets `M|C|W|I` (`0x100D`: MMU, write-back
D-cache, write buffer, I-cache) with `S|R` cleared, flushes prefetch, and returns
through an identity-mapped instruction stream. Disable cleans and invalidates the
8 KiB 4-way/64-set D-cache by set/way (`mcr p15, 0, r3, c7, c14, 2`) and drains
the write buffer before clearing `M|C|W|I`.

### Cache maintenance and DMA coherency

`include/vflash/mmu.h` exports ARM926EJ-S (32-byte cache line) maintenance helpers:

- `vflash_dcache_clean_range(addr, length)`: cleans dirty D-cache lines by MVA (`c7, c10, 1`) and drains the write buffer (`c7, c10, 4`). Call before CPU-to-device DMA (GPU command lists, texture uploads, video scanout, audio ring writes, CD-ROM write/scratch buffers).
- `vflash_dcache_invalidate_range(addr, length)`: cleans+invalidates any unaligned boundary lines (`c7, c14, 1`) and invalidates interior lines (`c7, c6, 1`). Call after device-to-CPU DMA (CD-ROM sector reads).
- `vflash_dcache_clean_invalidate_range(addr, length)`: cleans+invalidates D-cache lines (`c7, c14, 1`) and drains the write buffer.
- `vflash_icache_invalidate_all()`: invalidates the entire I-cache (`c7, c5, 0`) and drains prefetch.

On host builds these compile to inline no-ops. All SDK hardware drivers (`gpu_zevio.c`, `gpu.c`, `video.c`, `audio.c`, `cd.c`) call these helpers automatically around DMA transfers.

Descriptor and CP15 references: Arm DDI0198D sections 2.3.3–2.3.9,
3.2, 3.4 and 3.6, hosted by the processor vendor:
[ARM926EJ-S TRM](https://ww1.microchip.com/downloads/aemDocuments/documents/MPU32/ProductDocuments/ReferenceManuals/ARM_926EJS_TRM.pdf).
IFSR uses `mrc p15,0,Rd,c5,c0,1`; DFSR uses opcode_2=0, FAR c6,c0,0.
ARMv5 FSR does not supply the modern WnR bit. Future abort capture must preserve
these distinctions and use the proper LR offset per exception and state.

## Run the opt-in diagnostics

```sh
cmake --build build --target mmu_identity_disc_image mmu_undefined_disc_image
python3 tools/emulator_smoke.py --suite mmu-identity --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --out build/mmu-identity
python3 tools/emulator_smoke.py --suite mmu-undefined --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --out build/mmu-undefined
```

The identity case reads/writes SDRAM through a nonidentity 4 KiB alias at
0x20000000, refuses an active edit, renders Hello World, disables translation,
remaps the alias to a second physical page while off, then re-enables and
verifies the new contents. The
undefined case then executes a real ARM UDF with translation enabled. The
runner validates the exact exception PC/LR, crash record, panic native pixels
and actual screenshot; it does not synthesize any exceptions. Per-run results
are in `mmu-result.json`; original boot evidence/hashes remain in `result.json`.

Observed initial bring-up: control 0x79 enabled / 0x78 disabled on this model,
initialization 2 timer ticks (200 microseconds, timer resolution 100 us).
Table memory is 20480 bytes. A separate identity-mapped animation comparison
reported 60 FPS in both modes, 13.3 ms redraw off versus 13.4 ms on, and
16.7 ms frame intervals in both samples (100 us timer resolution). Each run
completed 794 frames and 796 buffer flips with exact scanout verification.
These are emulator observations; the model does not charge accurate TLB-miss
latency and cannot establish physical timing or cache correctness.

```sh
cmake --build build --target mmu_animation_disc_image
python3 tools/emulator_smoke.py --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --elf build/vflash_mmu_animation.elf \
  --disc build/mmu/animation/homebrew.cue --out build/mmu-on-animation --verify-animation
```

All 44 host tests and the three existing ELF contracts passed. The MMU-off
animation regression passed. The opt-in identity test verified all native
Hello World pixels and actual scanout, and the MMU-enabled UDF test verified
the exact fault PC/LR plus native panic framebuffer and scanout. Existing
ordinary program sources do not enable translation unless explicitly compiled
with the dedicated MMU animation definition.

## Remaining acceptance criteria

Genuine data/prefetch abort delivery, reliable fault-register population,
privileged read-only protection, domain violations, stack guards, active-page
permission transitions and protected animation benchmarks remain incomplete.
The existing panic handler is unchanged: no abort vectors are installed.
Do not test unmapped CPU access on this emulator and call the resulting zero
read, ignored write or secondary UND a data/prefetch abort.

Next implement and independently test emulator permission checks and abort
commit semantics, including LDM/STM/SWP and fetch fast paths; fix IFSR selection;
then extend panic capture and fault diagnostics. Re-run firmware and commercial
baseline regressions against that isolated emulator patch before enabling
protected stacks. Physical hardware remains untested.
