# Source provenance and platform notes

The SDK is a standalone C/assembly CMake project. Firmware-loader observations
are in [firmware.md](firmware.md); current behavior and commands are indexed in
[the documentation guide](../README.md). Research findings describe the pinned
source revisions below and do not establish physical-console compatibility.

Original firmware and commercial samples remain private local inputs.

## Freestanding runtime and engine integration notes

* Target-specific compiler and linker flags are centralized in
  `cmake/toolchain-vflash.cmake` and `cmake/VFlashTargets.cmake`.
* Upstream SDL2 is integrated as an unmodified submodule in `external/SDL2`
  with freestanding V.Flash video, renderer, joystick/gamecontroller, audio,
  and ISO 9660 `SDL_RWops` backends under `src/graphics/sdl2_vflash/`.
* This SDK enables C and assembly only; `-fno-exceptions`, `-fno-rtti`, and
  `-fno-unwind-tables` are candidates if an isolated C++ target is added later,
  after auditing allocation budgets and single-thread behavior.

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
