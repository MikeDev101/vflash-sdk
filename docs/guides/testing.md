# Emulator testing

Tested: FlashEm commit `fb109dd01a49239c5884fcec60b9dc451fb651f8`, Linux SDL2
standalone interpreter. [Upstream source](https://github.com/WizzardSK/flashem-libretro/tree/fb109dd01a49239c5884fcec60b9dc451fb651f8)
also documents other hosts, which were not tested here. `make flashem` needs
a C compiler, Make, SDL2 development files and libm; it does not need the
optional dynarmic submodule. Run the commands below from the repository root.

## Build the pinned emulator and check firmware boot

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
under `build/video-smoke`. See [video implementation and evidence](../reference/video.md),
[firmware findings](../research/firmware.md), and the [initial boot report](../history/initial-boot.md).

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

## Two distinct tests

* Direct mode omits `--bios`/`--disc`, creates a self-authored four-byte looping
  ROM, and uses FlashEm's `--load` ELF path. This validates the independent
  toolchain/startup path but bypasses disc boot.
* Firmware mode supplies both `--bios` and `--disc` plus the disc ELF for symbol
  lookup. The command contains **no `--load` or `--entry`**. Original firmware
  executes from reset, mounts the generated BIN/CUE, reads our generated
  `0SYSTEM/BOOT.BIN`, and launches its entry stub. The runner requires traces
  for both that stub and `_start`, then checks all expected words in SDRAM.

The runner copies only the emulator executable into a temporary runtime folder
to isolate ROM search and config writes. The original firmware is read from the
specified path. It clears inherited `VFLASH_*` settings, uses dummy SDL drivers,
sets deterministic RTC start, supplies frame and wall-clock limits, and records
`run.log`, `ram.bin`, and `result.json`. Missing/wrong markers, missing entry
traces, process errors, timeouts, and missing RAM dumps all fail the test.

The current graphical acceptance command additionally passes `--verify-video`.
It compiles the shared C renderer on the host and compares all native RAM pixels
and the complete actual `frame.ppm` scanout against it, while preserving the
original boot checks. It also checks `video_status`/`render_status` and rejects
unmodeled MMIO originating from the application. See [video.md](../reference/video.md).

## Interactive debugging

```sh
FLASHEM_BIOS='/path/to/A29L160ATV@TSOP48.BIN' \
  /path/to/flashem --dbg build/homebrew.cue
```

Obtain payload symbol addresses from its map or `arm-none-eabi-nm`. The disc entry `_start` is `0x10C00018`; resolve the current C entry
`vflash_main` and framebuffer `video_pixels` from that build's ELF. At the debugger prompt, `b 10c00018` sets an
entry breakpoint, `c` continues, `r` prints registers, `d 10c00018 20`
disassembles, and `m <symbol-address> 4` inspects the current probe data. Addresses after
startup can change on rebuild. `s N` steps and `q` quits.

The tested firmware's final loader branch is `0x1001268C`; the older upstream
note's `0x100126B8` is a literal-pool address in this dump. Always identify ROMs
by hash and verify addresses rather than assuming revision equivalence.

## Disc and accuracy limits

Load `homebrew.cue` with its generated `homebrew.bin` beside it. A 2048-byte
ISO omits the integrity bytes used by the guest's software EDC/ECC checks;
renaming it to `.bin` cannot add them. The raw helper supports only one Mode 1
track and no subchannels, audio tracks, or physical drive mastering.

Firmware-driven boot of our minimal generated track has been tested in the
emulator. The current program displays CPU-rendered Hello World and an RGB bar.
A mounted filesystem or screenshot alone would not establish execution; the
traces, marker and full framebuffer/scanout comparisons do. Physical V.Flash
hardware remains untested and is outside the current evaluation scope. CPU/device
timing and some peripheral behavior in FlashEm are approximate; emulator success
does not establish CD-R compatibility, media authentication behavior, electrical
behavior, or correct device initialization on a real console.

## Animation, panic and font checks

```sh
cmake --build build --target animation_disc_image
python3 tools/emulator_smoke.py \
  --emulator build/emulator/flashem --bios 'dumps/A29L160ATV@TSOP48.BIN' \
  --elf build/vflash_animation_probe.elf --disc build/animation/homebrew.cue \
  --out build/animation-smoke --verify-animation
./scripts/panic/build-undefined.sh
python3 tools/panic_smoke.py \
  --emulator build/emulator/flashem --bios 'dumps/A29L160ATV@TSOP48.BIN' \
  --test undefined --out build/panic-acceptance/undefined
python3 tools/font_preview.py build/font-preview.ppm
```

The font atlas is a host preview. Firmware acceptance images come from actual
emulator scanout and are checked against host references, never injected into
guest memory. See the [animation](../reference/animation.md),
[panic](../reference/panic.md) and [font](../reference/fonts.md) references for
full acceptance criteria. Run `ctest --test-dir build --output-on-failure` for
host tests and the three ELF contracts. These checks need no original ROM.
