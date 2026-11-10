# V.Tech V.Flash Experimental SDK

A freestanding GNU ARM toolchain and minimal homebrew SDK for the V.Tech 
V.Flash / V.Smile Pro. 

All evaluation targets the FlashEm emulator; physical hardware is currently untested.

No firmware, commercial executable, or proprietary SDK is included.
Supply your own local ROM for the firmware boot test.

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
not measure host wall-clock speed or establish ScratchEverywhere performance.
See [animation timing, memory ownership and verification](docs/animation.md).

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

## Build the probe and disc

Requirements: CMake 3.20+, Python 3.9+, GNU Arm Embedded GCC/binutils, and Ninja
or Make. ISO authoring additionally requires `xorriso`, `genisoimage`, or
`mkisofs`. Tested on Linux with GCC 14.2.1 and binutils 2.44; no libc/newlib or
C++ library is needed. Host drawing tests and visual verification also use
native `cc` (or the compiler executable specified by `VFLASH_HOST_CC`).

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target disc_image
```

Use `-DVFLASH_CROSS_PREFIX=/path/to/bin/arm-none-eabi-` for a compiler outside
PATH. Missing tools stop configuration. Omit `-G Ninja` to use Make.

Outputs:

* `build/vflash_probe.elf`: independent direct-load experiment at `0x10C00000`.
* `build/vflash_disc_probe.elf`: disc payload linked at `0x10C00018`.
* `.bin`, `.map`, `.dis`, `.readelf.txt` companions for each ELF.
* `build/BOOT.BIN`: deterministic, sector-padded executable, generated from
  the disc ELF without a commercial template.
* `build/homebrew.iso`: ISO 9660 filesystem containing `0SYSTEM/BOOT.BIN;1`.
* `build/homebrew.bin` and `build/homebrew.cue`: single Mode 1/2352 track with
  generated EDC/ECC. **Open the CUE for the firmware boot test.**

The demo sets its stack, clears `.bss`, preserves the boot checks, draws every
pixel in software, configures a linear 320×240 xBGR1555 scanout layer, and loops.
There is no graphics acceleration, audio, heap, libc, threading, or filesystem
dependency. Flags select ARM926EJ-S, ARM state, little endian,
AAPCS/EABI5, and software floating point. The two linker layouts and their
limits are explained in [hardware.md](docs/hardware.md).

## Test a real firmware-driven boot in FlashEm

Build the pinned public emulator; the standalone target needs a host compiler,
Make, SDL2 development files (`libsdl2-dev` on Debian/Ubuntu), and libm.

```sh
git clone https://github.com/WizzardSK/flashem-libretro.git /tmp/flashem-sdk-test
git -C /tmp/flashem-sdk-test checkout fb109dd01a49239c5884fcec60b9dc451fb651f8
make -C /tmp/flashem-sdk-test -j4 flashem
python3 tools/emulator_smoke.py \
  --emulator /tmp/flashem-sdk-test/flashem \
  --bios '/path/to/A29L160ATV@TSOP48.BIN' \
  --disc build/homebrew.cue --elf build/vflash_disc_probe.elf \
  --out build/video-smoke --verify-video
```

Tested ROM SHA-256:
`467744d11dcf011762f41db4be60efbbd63cd2420003a1de41c421b4c88533ba`.
The filename alone does not establish identity or authenticity. Other revisions
are untested. The runner uses the ELF for symbol lookup only; it **does not pass
`--load`** in firmware mode. It checks entry traces and the original three words
in an actual SDRAM dump. `--verify-video` also checks video/render status,
the complete native framebuffer and the actual emulator screenshot against
the same C renderer compiled on the host. It uses dummy SDL drivers, 1200
emulator frames, and a 120-second
host timeout. `--frames` and `--timeout` can adjust these bounds.

Expected values: `boot_marker = 0x56464C53`, `startup_status = 0x600D600D`, and
`data_cookie = 0x13579BDF`. The visual test also expects
`video_status = 0x56494431` and `render_status = 0x44524157`.
Evidence, hashes, log, RAM dump, and actual `frame.ppm` screenshot are saved
under `build/video-smoke`. See [video implementation and evidence](docs/video.md),
[firmware findings](docs/firmware.md), and the [initial boot report](docs/report.md).

To preview the same C rendering on the host independently:

```sh
python3 tools/render_preview.py build/render-reference
```

Open `build/render-reference/frame.ppm`; `frame.bgr555` and `reference.json`
provide native pixels and hashes. This is a host preview; the separate
`build/video-smoke/frame.ppm` comes from FlashEm's actual presentation path.

The independent direct-loader test needs no original firmware:

```sh
python3 tools/emulator_smoke.py \
  --emulator /tmp/flashem-sdk-test/flashem \
  --elf build/vflash_probe.elf --out build/emulator-smoke
```

That mode uses our own four-byte synthetic ROM and `--load`, and does not test
disc boot. Both modes isolate ROM lookup/configuration writes in a temporary
runtime directory and remove stale success evidence before running.
For a direct-load visual test, add `--verify-video`; it defaults to 60 frames
to allow cold-start software rendering to finish. Firmware visual acceptance
uses the 1200-frame disc boot test above.

## Inspect, package, and master manually

```sh
python3 tools/inspect_boot.py /path/to/BOOT.BIN
python3 tools/verify_elf.py build/vflash_disc_probe.elf
python3 tools/pack_boot.py build/vflash_disc_probe.elf /tmp/probe-BOOT.BIN --verbose
sh tools/make_iso.sh /tmp/probe-BOOT.BIN /tmp/probe.iso
python3 tools/raw_cd.py /tmp/probe.iso /tmp/probe.cue
```

These commands refuse existing output files. The inspector is read only,
bounds checked, capped at 64 MiB, and explicitly treats `REL\0` / `DBG\0`
signature hits as observations rather than parsed chunks. The packer supports
only the tested fixed-address takeover model; it does not implement a µMORE
application ABI, relocation, or compression. BOOT output is deterministic;
ISO/BIN hashes can vary with filesystem dates and generator metadata.

See [boot format](docs/boot-format.md) and [emulator procedures](docs/emulator.md).
