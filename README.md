# V.Tech V.Flash Experimental SDK

A freestanding GNU Arm C/C++ and assembly SDK for the V.Tech V.Flash / V.Smile
Pro. Homebrew boots through the original firmware in FlashEm and targets the
ARM926EJ-S CPU and Zevio 2D/3D hardware. Evaluation targets the pinned
emulator; physical hardware is currently untested.

## Features

- **Graphics & Display:** Double-buffered 320x240 BGR555 linear framebuffer,
  5x7 printable ASCII font, software drawing primitives, Zevio 2D/3D GPU HAL,
  and an upstream SDL2 port (`SDL2` + Zevio 2D renderer).
  - **Native GPU Renderer:** Double-buffered `1024x240` tiled surfaces
    (`320x240` playfield at `(0, 0)` plus a `704x240` offscreen texture atlas at
    `(320, 0)`), supporting hardware clipping, quads, textured/tinted/alpha-blended
    sprites, textured triangles (`SDL_RenderGeometry`), RGB color modulation,
    offscreen render targets, and per-tile dirty-bitmask synchronization between
    the two tiled surfaces.
  - **SDL2 Limitations:** Full-texture `SDL_UpdateTexture()` only (no partial
    rect updates or `SDL_TEXTUREACCESS_STREAMING` lock/unlock), static
    `704x240` atlas without runtime compaction, and even texture widths for
    non-target textures.
- **Audio & Video:** Native PCM audio ring-buffer driver (`S16LE` / `U8`),
  Motion JPEG (`MJP`) video + ADPCM/PCM playback via PicoJPEG, and a
  double-buffered loading screen.
- **Storage & Input:** Sony CXD3068Q CD-ROM sector driver, read-only ISO 9660
  filesystem, and UART0 controller input (D-pad, action buttons, analog stick).
- **System & Diagnostics:** 10 kHz Timer 0 clock, optional ARMv5 MMU identity
  and page/section mapping, C++ freestanding runtime support, and an ARM panic
  handler with undefined-instruction / SWI diagnostics.

No firmware, commercial executable, or proprietary SDK is included. Supply your
own local ROM; game dumps and save cards stay in ignored local directories.

## Build

Requires CMake 3.20+, Python 3.9+, GNU Arm Embedded GCC/binutils, and Ninja or
Make. Disc images also require `xorriso`, `genisoimage`, or `mkisofs`; host
drawing tests require native `cc` (or `VFLASH_HOST_CC`).

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-vflash.cmake
cmake --build build --target all disc_image
ctest --test-dir build --output-on-failure
```

The default disc image is `build/homebrew.cue` with its companion `.bin`.
Additional targets, custom toolchains, and packaging commands are in the
[build guide](docs/guides/building.md).

## Examples & Demos

All example programs, demo launchers, and FlashEm instructions are available in the [examples guide](examples/README.md).

## Verification

Run the host unit tests and emulator acceptance suites:

```sh
python3 -m unittest discover -s tests -v
python3 tools/emulator_smoke.py --suite suite
python3 tools/emulator_smoke.py --suite bad-apple
python3 tools/emulator_smoke.py --suite fmv
for t in atlas-frag color-mod dirty-sync lifecycle renderer sdl2 triangle-tex; do
  python3 tools/gpu_smoke.py --test "$t" --mmu both
done
```

For a single-disc firmware boot and scanout check:

```sh
python3 tools/emulator_smoke.py \
  --emulator build/emulator/flashem --bios 'dumps/A29L160ATV@TSOP48.BIN' \
  --elf build/vflash_disc_probe.elf --disc build/homebrew.cue \
  --out build/video-smoke --verify-video
```

See the [test guide](docs/guides/testing.md) for emulator setup, direct ELF
loading, and debugging options.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `include/vflash/` | Public SDK C headers (`vflash/video.h`, `vflash/audio.h`, etc.) |
| `include/sdl2_vflash/` | Freestanding SDL2 configuration and V.Flash extension header |
| `src/gpu/` | Software and Zevio 2D/3D GPU HAL backends |
| `src/graphics/` | CPU drawing, 5x7 font, PicoJPEG, Zevio 2D renderer, SDL2 port |
| `src/platform/` | Video scanout, MJP player, CD-ROM, ISO 9660 FS, audio, input, clock, MMU, loading screen |
| `src/runtime/` | Bounded allocator, panic capture, and crash screen renderer |
| `startup/` | ARM startup/exception assembly and linker scripts |
| `examples/` | Demos, benchmarks, C++ test, SDL2 demos, and unified test suite |
| `cmake/` | Cross toolchain, program targets, and disc image rules |
| `scripts/panic/` | Standalone panic diagnostic build scripts |
| `tools/`, `tests/` | Host tooling, asset staging, smoke runner, and unit tests |
| `docs/` | Guides, hardware/API references, and research notes |
| `HACKING.md`, `.clang-format` | Coding style guidelines and formatting configuration |

## Documentation

- [Documentation index](docs/README.md) · [Coding style](HACKING.md) · [Source provenance](docs/research/sources.md)
- [GPU HAL](docs/reference/gpu.md) · [Native tiled HAL](docs/reference/gpu-native-hal.md) · [Zevio register inventory](docs/reference/zevio-registers.md)
- [MMU reference](docs/reference/mmu.md) · [Font atlas](docs/reference/fonts.md) · [Bad Apple!! benchmarks](docs/reference/bad-apple-benchmarks.md)
