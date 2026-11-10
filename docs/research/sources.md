# Source provenance and future integration

The SDK is a standalone C/assembly CMake project. Firmware-loader observations
are in [firmware.md](firmware.md); current behavior and commands are indexed in
[the documentation guide](../README.md). Research findings describe the pinned
source revisions below and do not establish physical-console compatibility.

No ScratchEverywhere or SDL2 platform port is implemented. The public-source
audit below is retained as context for that future work, not as a list of SDK
features. Original firmware and commercial samples remain private local inputs.

## ScratchEverywhere inspection

The public upstream was inspected at commit
[`23b054a4ecfa8815786eb7e47267b7c7beae0101`](https://github.com/ScratchEverywhere/ScratchEverywhere/tree/23b054a4ecfa8815786eb7e47267b7c7beae0101).
It was not built, modified, or integrated into this SDK.

* Root `CMakeLists.txt` selects platform modules under `cmake/platforms` and
  separate renderer, windowing, and audio modules. Most non-MSVC builds use C++17.
* Embedded/console backends exist, including NDS/3DS, but no general freestanding
  target was found in the platform configuration. Headless renderer/windowing
  modules do not establish a freestanding C++ runtime.
* Xbox uses `$NXDK_DIR/share/toolchain-nxdk.cmake` with `-DXBOX=ON`; its Dockerfile
  places nxdk's wrappers in PATH. The source recipe builds/patches nxdk when
  needed, and `cmake/platforms/xbox.cmake` selects Pentium III options, runtime
  libraries, automount/network support, and executable packaging using `cxbe`
  and `extract-xiso`. Those dependencies and x86 flags do not transfer to ARM.
* Platform isolation and target-specific compiler flags can be reused as design
  patterns. Xbox executable/disc packaging cannot be reused as V.Flash packaging.

For a future port, begin with `cmake/platforms/vflash.cmake`, corresponding
`source/platforms/vflash` code, and explicit renderer/windowing/audio choices.
First resolve C++ runtime needs, libc substitutes, allocation budget, project
loading, software math, single-thread behavior, native input and framebuffer.
`-fno-exceptions`, `-fno-rtti`, and `-fno-unwind-tables` are candidates for a later
isolated C++ target, but require auditing upstream dependencies. This SDK
enables C and assembly only; it does not claim C++ runtime support.

## Source provenance

| Source | Revision / purpose |
| --- | --- |
| [FlashEm](https://github.com/WizzardSK/flashem-libretro/tree/fb109dd01a49239c5884fcec60b9dc451fb651f8) | `fb109dd01a49239c5884fcec60b9dc451fb651f8`, source inspected and standalone executable tested |
| [Memory model](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/hw.c) | `RAM_BASE`, `RAM_SIZE`, `hw_load_phys`, register model, RAM-dump mechanism |
| [Program loader](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/vflash.c) | `vflash_load_program`, ELF and partial BOOT interpretation, reset context |
| [Frontend](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/main.c) | `--load`, `--entry`, debugger and headless switches |
| [Boot working notes](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/CLAUDE.md) | Observed BOOT base and entry instruction; research assertions, not a full schema |
| [Original disc inspection](https://multimedia.cx/eggs/vdisc-analysis/) | Firsthand report from 2006-12-24; path and signature observations |
| [GCC ARM target options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html) | CPU selection, ARM state, endianness, ABI and soft float |

Research snapshot date: 2026-11-10. Pin source revisions when reproducing work;
emulator command lines, models, and observations may change on the default branch.
