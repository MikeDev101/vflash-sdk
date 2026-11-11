# V.Tech V.Flash Experimental SDK

A freestanding GNU Arm C/assembly SDK for the V.Tech V.Flash / V.Smile Pro.
Homebrew boots through the original firmware in FlashEm and renders through the
CPU framebuffer. Evaluation targets the pinned emulator; physical hardware is
currently untested.

The baseline includes Hello World, a double-buffered animation with device-time
counters, a 95-character printable ASCII font, a panic handler with genuine
ARM undefined-instruction diagnostics, native 2D/3D Zevio GPU acceleration,
controller input, PCM audio, ISO 9660 disc file I/O, and an upstream SDL2 port.
Data/prefetch aborts and IRQ/FIQ are not validated. There is no C++ runtime yet.

No firmware, commercial executable or proprietary SDK is included. Supply your
own local ROM; game dumps and save cards stay in ignored local directories.

## Build

Requires CMake 3.20+, Python 3.9+, GNU Arm Embedded GCC/binutils, and Ninja or
Make. Disc images also require `xorriso`, `genisoimage` or `mkisofs`; host drawing
tests require native `cc` (or `VFLASH_HOST_CC`).

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-vflash.cmake
cmake --build build --target all disc_image
ctest --test-dir build --output-on-failure
```

The static disc is `build/homebrew.cue` with its companion BIN. Other outputs,
custom toolchains and packaging commands are in the [build guide](docs/guides/building.md).

## Try the demos

Place your local 2 MiB firmware at `dumps/A29L160ATV@TSOP48.BIN` and provide a
FlashEm executable at `build/emulator/flashem`, through `FLASHEM`, or via
`--emulator /path/to/flashem`. Each homebrew launcher builds its disc as needed.

```sh
./play-homebrew.sh
./play-homebrew.sh --demo animation
./play-homebrew.sh --demo panic
./play-homebrew.sh --demo mmu-identity
./play-homebrew.sh --demo mmu-undefined
./play-homebrew.sh --demo mmu-animation
./play-homebrew.sh --demo gpu-software
./play-homebrew.sh --demo gpu-mmu
./play-homebrew.sh --demo gpu-native
./play-homebrew.sh --demo gpu-tiled --gpu-mmu on --gpu-view b
./play-homebrew.sh --demo gpu-frame --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-sprite --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-transparent --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-alpha --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-modes --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-renderer --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-lifecycle --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-sdl --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-sdl2 --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-sdl2-draw --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-triangle --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-triangle-depth --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-triangle-tex --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-cube --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo input --gpu-mmu on
./play-homebrew.sh --demo sdl2-input --gpu-mmu on
./play-homebrew.sh --demo audio --gpu-mmu on
./play-homebrew.sh --demo sdl2-audio --gpu-mmu on
./play-homebrew.sh --demo fs --gpu-mmu on
./play-homebrew.sh --demo sdl2-asset --gpu-mmu on
./play-homebrew.sh --demo suite
./open-bios.sh
./test-game.sh
```

`--bios /path/to/ROM` and `--scale 1` through `--scale 4` override the defaults.
F11 toggles fullscreen, F5 saves a screenshot, and Esc quits. Add `--memcard` to
attach/create `cards/card1.bin`; existing saves are preserved. See the
[run guide](docs/guides/running.md) for game selection, controls, BIOS behavior
and memory-card persistence.

Build a separate diagnostic without opening FlashEm:

```sh
./scripts/panic/build-undefined.sh
./scripts/panic/build-manual.sh --parallel 4
```

All ten build scripts now live under `scripts/panic/`. The main root launchers
and CMake target/output names remain unchanged. The full diagnostic list is in
the [panic reference](docs/reference/panic.md).

## Verify firmware boot

```sh
python3 tools/emulator_smoke.py \
  --emulator build/emulator/flashem --bios 'dumps/A29L160ATV@TSOP48.BIN' \
  --elf build/vflash_disc_probe.elf --disc build/homebrew.cue \
  --out build/video-smoke --verify-video
```

This checks the firmware-to-application traces, SDRAM markers, native pixels
and actual scanout. The ELF is used for symbols, never injected in firmware
mode. The [test guide](docs/guides/testing.md) covers the pinned emulator build,
direct loading, animation, panic diagnostics and debugging.

## Repository layout

| Path | Purpose |
| --- | --- |
| `include/vflash/` | Public SDK C headers; include as `vflash/font.h`, etc. |
| `src/graphics/` | CPU drawing and bitmap font |
| `src/platform/` | Video and polled device timer |
| `src/runtime/` | Panic capture and emergency rendering |
| `startup/` | ARM startup/exception assembly and linker scripts |
| `examples/` | Hello World, animation, shared scene and panic diagnostics |
| `cmake/` | Cross toolchain, program targets and shared disc rules |
| `scripts/panic/` | One build entry point per diagnostic |
| `tools/`, `tests/` | Host tooling and verification |
| `docs/` | Guides, implementation references, research and historical reports |

[Documentation index](docs/README.md) · [Font atlas](docs/reference/fonts.md) ·
[Source provenance](docs/research/sources.md)

Optional MMU translation diagnostics and the remaining memory-protection gaps
are documented in [the MMU reference](docs/reference/mmu.md). Normal applications
continue with the MMU disabled.

The [GPU HAL](docs/reference/gpu.md) supplies software fills/blits and a separate
command-driven native-fill experiment; a restricted native tiled HAL is now available for tile-aligned surfaces (`32x8` up to `1024x1024`, direct presentation up to `320x240`)
(see [the native HAL contract](docs/reference/gpu-native-hal.md)). See the [register inventory](docs/reference/zevio-registers.md).

[Direct tiled presentation experiments](docs/reference/gpu-tiled.md) verify
conversion-free scanout, buffer swaps, linear recovery, D2 region copies and
D1 readback in FlashEm, including MMU-on tests.

[Native HAL frame tests](docs/reference/gpu-native-hal.md) exercise generalized
tiled geometry, fills, linear-to-tiled uploads, same-surface copies, direct
vblank presentation and shutdown recovery with layout-specific capabilities and
MMU off/on.
