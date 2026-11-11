# Run FlashEm, firmware and games

All commands below run from the repository root. Local ROMs, game dumps and
save cards are not part of the SDK source.

## Open the graphical demo

From this repository, run:

```sh
./play-homebrew.sh
```

The launcher builds the disc if necessary, finds the local ROM in `dumps/`,
and opens FlashEm at 3× scale. It uses the original firmware disc-boot path,
with no ELF injection, and stays open until you quit. **F11** toggles fullscreen,
**F5** saves `build/play/screenshot.bmp`, and **Esc** quits. The demo is static;
there are no game/controller interactions yet.

A compiled emulator copy is available locally at `build/emulator/flashem`.
After a fresh checkout/build-directory cleanup, use a separately built emulator:

```sh
./play-homebrew.sh --emulator /path/to/flashem --bios /path/to/boot-ROM
```

`--scale 1` through `--scale 4` selects window size. `FLASHEM` can also specify
the emulator executable. The launcher disables GE-count-based fast-boot pacing
for the CPU-only static demo. Private files under `dumps/` remain Git-ignored.

## Animated software-rendering demo

```sh
./play-homebrew.sh --demo animation
```

This separately built disc shows a bouncing square, double buffering, measured
presented FPS and CPU redraw milliseconds. It uses the same firmware boot path
as Hello World. Use `--demo hello` (or omit `--demo`) for the static demo.
The usual `--scale`, `--bios`, `--emulator` and optional `--memcard` work here.

The initial FlashEm measurement was 60 presented FPS with a 13.4 ms full redraw.
These are device-clock measurements under the emulator's timing model; they do
not measure host wall-clock speed.
See [animation timing, memory ownership and verification](../reference/animation.md).

```sh
cmake --build build --target animation_disc_image
python3 tools/emulator_smoke.py \
  --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' \
  --elf build/vflash_animation_probe.elf \
  --disc build/animation/homebrew.cue \
  --out build/animation-smoke --verify-animation
```

Images are under `build/animation/`; interactive settings and F5 screenshots
use `build/play-animation/`. Automated tests check alternating scanout banks,
visible movement, status markers, timing and exact native/RGB output against
the same C renderer on the host. The static pixel reference remains available.

## Open the BIOS menu

```sh
./open-bios.sh
```

This starts the local NOR firmware with an empty drive and skips building or
loading homebrew. Allow time for BIOS startup; the tested firmware reaches the
system menu and shows an “Insert V.DISC” prompt. The launcher uses FlashEm's
`--dbg-run` option to suppress its automatic disc picker while starting
unpaused; no debugger interaction is required. F2 can pause/resume it.

The same `--emulator`, `--bios`, and `--scale` options are available:

```sh
./open-bios.sh --scale 2
```

**F11** toggles fullscreen, **F5** saves `build/bios/screenshot.bmp`, and **Esc**
quits. BIOS-mode settings are isolated in `build/bios/`. The homebrew launcher
continues using `build/play/`.

## Test a dumped game

```sh
./test-game.sh
```

The launcher searches recursively under `dumps/` for extracted CUE/ISO disc
images. It automatically boots the only game found, or asks you to choose by
number when several are available in a terminal. A matching CUE is preferred
over an ISO so the raw sector data and track metadata are available. Keep the
CUE and its referenced BIN together; ZIP files are not loaded directly.
Firmware dumps, extracted BOOT.BIN files and memory-card BINs are excluded
from automatic discovery.

```sh
./test-game.sh --list
./test-game.sh --memcard
./test-game.sh --disc "dumps/Game Folder/Game.cue" --memcard cards/card1.bin
```

`--disc` explicitly selects an image; raw CD BINs are also accepted there,
although their CUE is preferable. With multiple dumps and no terminal, use
`--disc` rather than an interactive selection. `--emulator`, `--bios` and
`--scale` work as in the other launchers.

Game mode skips the homebrew build and boots the selected disc through the
NOR firmware with no `--load` or ELF injection. Runtime settings and F5
screenshots use `build/game/`. No card is attached unless `--memcard` is given.
Arrow keys control directions; Z/X/C/V are the red/yellow/green/blue buttons,
Enter is OK, Backspace is Exit, F11 toggles fullscreen, and Esc closes FlashEm.

## Virtual memory cards

Attach the default card to any launcher:

```sh
./open-bios.sh --memcard
./play-homebrew.sh --memcard
./test-game.sh --memcard
```

`--memcard` creates or reuses `cards/card1.bin`. A new image is an erased,
unformatted 8 MiB raw flash card; if the BIOS asks to initialize/format it,
use the BIOS memory-card menu. An existing image is never reset by the launcher.
To select a different card, supply a filename:

```sh
./open-bios.sh --memcard cards/card2.bin
./play-homebrew.sh --memcard cards/card2.bin
```

Paths are resolved before the emulator changes directory. Images must be
exactly 8,388,608 bytes; incorrect-size inputs are rejected rather than silently
truncated. FlashEm exposes one card at a time. Without `--memcard`, no card is
inserted. Use one running emulator per card image, and quit with Esc or close
the window normally so FlashEm writes changed card data back to the file.

`cards/` is Git-ignored and lives outside `build/`, so rebuilding/cleaning build
artifacts does not delete saved cards. This uses the pinned emulator's native
`--memcard` support and SPI DataFlash model; no firmware or disc changes are
needed. A BIOS-only run with an attached blank card and clean shutdown was
verified, along with preservation of existing images and rejection of bad sizes.

## Panic diagnostics

```sh
./play-homebrew.sh --demo panic
./play-homebrew.sh --demo panic --panic-test manual
```

The separate diagnostic boots through firmware and executes an ARM undefined
instruction, then shows a blue panic screen and retains a versioned SDRAM crash
record. Normal demos install the UND handler without deliberately faulting.
The handler uses a dedicated stack/framebuffer; SWI is untouched by default.
Build an individual diagnostic with `./scripts/panic/build-undefined.sh` (or the
corresponding `scripts/panic/build-TEST.sh` script for any of the ten cases).
See [panic support, limitations and acceptance commands](../reference/panic.md).
Data/prefetch aborts and IRQ/FIQ are not validated; the pinned interpreter also
has Thumb exception limitations documented there.

## MMU, GPU, Input, Audio, Filesystem, and SDL2 demos

`./play-homebrew.sh` builds and boots all SDK samples and diagnostics:

```sh
./play-homebrew.sh --demo mmu-identity
./play-homebrew.sh --demo mmu-undefined
./play-homebrew.sh --demo mmu-animation
./play-homebrew.sh --demo gpu-software
./play-homebrew.sh --demo gpu-mmu
./play-homebrew.sh --demo gpu-native
./play-homebrew.sh --demo gpu-tiled --gpu-mmu off --gpu-view a
./play-homebrew.sh --demo gpu-frame --gpu-mmu on --gpu-view b
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
```

For multi-view GPU demos (`gpu-tiled`, `gpu-frame`, `gpu-sprite`,
`gpu-transparent`, `gpu-alpha`, `gpu-modes`, `gpu-renderer`, `gpu-lifecycle`,
`gpu-sdl`, `gpu-sdl2`, `gpu-sdl2-draw`, `gpu-triangle`, `gpu-triangle-depth`,
`gpu-triangle-tex`, `gpu-cube`), `--gpu-mmu` selects `off` (default) or `on`
(identity MMU with `0xA8000000` mapped), and `--gpu-view` selects `a`
(default), `b`, or `linear` (restored linear scanout after shutdown). For
`input`, `sdl2-input`, `audio`, `sdl2-audio`, `fs`, `sdl2-asset`, and `suite`,
`--gpu-mmu` selects `off` (default) or `on`. Each variant uses its own isolated
runtime directory under `build/play-*`. See [the MMU reference](../reference/mmu.md),
[the GPU HAL reference](../reference/gpu.md), and
[the native GPU HAL contract](../reference/gpu-native-hal.md).
