# Implementation report — 2026-11-10

This records the initial RAM-marker boot milestone. The subsequent CPU-rendered
Hello World milestone, current addresses/hashes, and emulator-only evaluation
scope are documented in [video.md](../reference/video.md). Historical sizes and addresses
below refer to the original minimal probe.

**A custom ARM program now boots through the supplied V.Flash firmware from
our generated homebrew disc in FlashEm, without `--load`.** Entry traces and
an actual SDRAM dump establish execution. Physical hardware is untested.

## Delivered

* GNU Arm toolchain configuration and freestanding ARM926EJ-S C/assembly probe.
* Separate direct-load and firmware-disc linker layouts, startup, ELF/map/raw
  binary/disassembly/readelf outputs and architecture/load checks.
* Bounded read-only BOOT inspector and deterministic fixed-address ELF-to-BOOT
  packer, based on the now-inspected firmware loader.
* ISO 9660 staging and a Python Mode 1/2352 BIN/CUE encoder with EDC/ECC.
* Bounded direct and firmware-boot smoke tests with marker checks and evidence.
* Synthetic malformed-input, packaging, sector-vector and ISO round-trip tests,
  source-pinned research, and reproducible build/run commands.

## Evidence

Host: Linux, GCC 14.2.1 Arm Embedded, binutils 2.44, CMake 3.31.6, Python 3.13.5,
genisoimage 1.1.11, SDL2 2.32.4. FlashEm interpreter source revision:
`fb109dd01a49239c5884fcec60b9dc451fb651f8`.

The supplied 2 MiB `A29L160ATV@TSOP48.BIN` has SHA-256
`467744d11dcf011762f41db4be60efbbd63cd2420003a1de41c421b4c88533ba`.
It boots in the emulator. Its filename/provenance alone does not establish
vendor authenticity; this report identifies the exact bits that were tested.

The firmware's load routine uses header `+4` as byte count, `+8` as destination,
and `+12` as entry offset. It clears CP15 control and enters SVC with IRQ/FIQ
masked before branching to `load + entry_offset`. This permitted a header,
entry stub, and self-contained payload without commercial REL/DBG metadata.
See [firmware.md](../research/firmware.md) for addresses and trace details.

The generated BOOT is 512 bytes and has SHA-256
`bac52dc7368c5f2d288043f720d773e7c22fab659c0c7054ce2c1b7ba559c237`.
The 176-sector generated disc contains only our homebrew executable and ISO
metadata, without commercial code or firmware. ISO/BIN hashes can vary with
generator timestamps; the BOOT packer is deterministic for a given ELF.

The firmware-driven run reached BOOT stub `0x10C00010`, startup `0x10C00018`,
and C entry `0x10C00048`. The RAM dump contained:

| Symbol | Disc build address | Observed value |
| --- | --- | --- |
| `data_cookie` | `0x10C000AC` | `0x13579BDF` |
| `startup_status` | `0x10C000B0` | `0x600D600D` |
| `boot_marker` | `0x10C000B4` | `0x56464C53` |

PC subsequently remained at `0x10C00088` in the deliberate loop, with SP
`0x11000000`. The independent synthetic-ROM/direct-ELF test also passed.
Each smoke run saves executable/ROM hashes and symbol-derived evidence in its
selected output directory. No direct program loader or guest-memory injection
is used by the firmware test.

Build and all three CTest suites pass: host tooling (17 unittest methods),
direct ELF contract, and disc ELF contract. Static inspection confirms ARMv5TEJ,
ARM state, EABI5 soft float, separate RX/RW segments and no runtime libraries.
Missing compiler and invalid-input checks fail cleanly. Sector generation was
cross-checked byte-for-byte against five local Mode 1 sectors, and a synthetic
sector matches an independent ECM implementation. Automated fixtures include
no commercial data.

## Work identified at that milestone (historical)

This is not the current roadmap. Physical hardware remains outside the current
evaluation scope; the subsequent display, animation, panic and font work is
covered by the [reference index](../README.md).

1. Test the generated disc on physical hardware, establishing optical/CD-R and
   media acceptance requirements without assuming emulator equivalence.
2. Validate additional firmware revisions and improve launch-context coverage
   before generalizing the linker/packer beyond this takeover model.
3. Add an observable UART or display test only after its initialization and
   ownership requirements are verified.
4. Measure usable RAM and runtime services before revisiting ScratchEverywhere.
   No ScratchEverywhere code or existing platform target was modified.

Commercial chunks, relocation/compression, warm µMORE application integration,
and hardware compatibility remain unresolved. The minimum emulator boot
milestone is complete. Local original assets remain untouched and Git-ignored;
no firmware or commercial content is shipped by this SDK.
