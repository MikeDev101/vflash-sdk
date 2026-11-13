# V.Flash SDK Examples & Demos

All example programs and diagnostic demos live under `examples/`. Each
launcher builds its disc image automatically as needed.

## Prerequisites

Place your local 2 MiB firmware at `dumps/A29L160ATV@TSOP48.BIN` and provide a
FlashEm executable at `build/emulator/flashem`, via `FLASHEM`, or with
`--emulator /path/to/flashem`.

## Running Demos

Run from the repository root with `./play-homebrew.sh`:

```sh
./play-homebrew.sh                      # Hello World (default)
./play-homebrew.sh --demo animation     # Double-buffered CPU animation
./play-homebrew.sh --demo suite         # Unified interactive/automated test suite
./play-homebrew.sh --demo bad-apple     # Bad Apple!! MJP + audio demo
./play-homebrew.sh --demo fmv           # MJP FMV player demo
./play-homebrew.sh --demo stress        # Combined MJP + audio stress benchmark
./play-homebrew.sh --demo cpp           # Freestanding C++ runtime + SDL2 demo
./play-homebrew.sh --demo input --gpu-mmu on
./play-homebrew.sh --demo sdl2-input --gpu-mmu on
./play-homebrew.sh --demo audio --gpu-mmu on
./play-homebrew.sh --demo sdl2-audio --gpu-mmu on
./play-homebrew.sh --demo fs --gpu-mmu on
./play-homebrew.sh --demo sdl2-asset --gpu-mmu on
./play-homebrew.sh --demo gpu-cube --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo gpu-sdl2-draw --gpu-mmu on --gpu-view a
./play-homebrew.sh --demo mmu-identity
./play-homebrew.sh --demo panic
./play-homebrew.sh --help               # Full list of demos and flags
```

### Launcher Options & Hotkeys

- `--bios /path/to/ROM`, `--scale 1`..`4`, and `--memcard` (attaches or creates
  `cards/card1.bin`, preserving existing saves).
- `./open-bios.sh` boots into the console BIOS; `./test-game.sh` boots a local
  dumped game.
- FlashEm hotkeys: `F11` toggles fullscreen, `F5` saves a screenshot, `Esc`
  quits.
- Standalone panic diagnostic builders live under `scripts/panic/` (see the
  [panic reference](../docs/reference/panic.md)).

See the [run guide](../docs/guides/running.md) for the complete demo list,
controller mappings, BIOS behavior, and memory-card persistence.

## Example Directories

| Directory | Contents |
| --- | --- |
| `animation/` | Double-buffered framebuffer animation with Timer 0 counters |
| `audio/` | Native PCM ring-buffer and SDL2 audio demos |
| `bad_apple/` | Bad Apple!! MJP video + audio demo and loading screen |
| `common/` | Shared scene/render helpers used across examples |
| `cpp/` | Freestanding C++ global constructors, virtuals, templates, and SDL2 test |
| `fmv/` | Interactive MJP Motion-JPEG video + audio player |
| `fs/` | Native ISO 9660 filesystem and SDL2 RWops/BMP/WAV asset demos |
| `gpu/` | Zevio 2D/3D GPU HAL, tiled surface, renderer, and SDL2 render demos |
| `hello/` | Minimal Hello World firmware boot probe |
| `input/` | Native UART0 controller and SDL2 joystick/controller demos |
| `mmu/` | ARMv5 MMU identity, undefined-instruction, and animation diagnostics |
| `panic/`, `panic_demo/` | Crash capture and exception-vector diagnostic demos |
| `stress/` | Sustained MJP + audio throughput and profiling benchmark |
| `suite/` | Unified 8-test interactive/automated SDK test suite + input diagnostics |
