# Tiled scanout, D2 copies and D1 readback experiment

This is an isolated FlashEm validation, not a physical Zevio programming
specification or a generally enabled hardware HAL backend. No SDL2 integration
or new acceleration performance claim is made.

## Pattern and storage

Two separate 64-byte-aligned SDRAM allocations each reserve 32768 bytes: two
rows of the model's 1024-pixel-wide, 32x8-unit tiled layout. The visible area
is 64x16, not a packed 2048-byte allocation. Surface A has four distinct 32x8
quadrants plus small yellow/magenta rectangles crossing x=32 and y=8. Surface B
has a different blue/red pattern with a 2x2 rectangle crossing both boundaries.
Every color is produced by native D3 commands. The CPU initializes sentinel
storage before submission and never writes either tiled surface afterward.

The host oracle checks every word of both allocations, including untouched
padding. The address formula is verified over neighboring horizontal units and
both vertical rows. This establishes the modeled address layout for the tested
regions, not a maximum command size or definitive physical tile geometry.

## Direct display and recovery

The experiment first uses normal linear video initialization, then points color
layer 0 directly at A and B, selecting B8000108 bit 2. It places the visible
64x16 region at (13,19), preserves the output extent and disabled IRQ mask, and
leaves each view active for 20 vblanks. It then calls the existing video driver
to restore the full linear window/address/layout. The linear buffer stays black
throughout: there is no CPU tiled-to-linear conversion or copy in this path.

Three final-view binaries preserve A, B or restored linear scanout for capture.
The runner verifies complete actual RGB screenshots for each view and logs
scanout-address transitions A -> B -> linear. Each variant also confirms
firmware exception trampolines remain intact. The existing video driver remains
unchanged; the temporary tiled configuration is private experimental code.

The same three views run with the optional MMU enabled. That variant explicitly
identity-maps the A800 section before activation in addition to the existing
profile. Command, surface and readback pointers therefore denote physical SDRAM
addresses. Caches/write buffering remain disabled as in MMU bring-up; arbitrary
virtual aliases and cache-enabled native execution remain untested.

## D2 and D1 boundaries

D2 uses ONE active backing-surface base in this model. It does not accept
independent source/destination base addresses. The test copies the visible A
region to an offscreen region at x=80 in the same allocation. It then copies a
60x14 portion four pixels right and two rows down to exercise overlap.
The oracle uses a source snapshot and compares every destination/padding word.
Passing establishes snapshot/memmove-like behavior for this modeled overlap
case only; physical overlap semantics and other directions remain unverified.
No general cross-allocation hardware blit capability is advertised.

D1 reads the resulting offscreen 64x16 region into a separate 2048-byte linear
buffer after setting the model's 0C destination pointer. All 1024 words are
checked against the independent expected result. This provides a second
observation of native memory distinct from scanout. D1 isn't needed for direct
presentation. No blending, transparency, clipping of D2, format conversion,
odd-width readback or out-of-bounds command behavior is claimed.

Five lists run per diagnostic: fill A, fill B, D2 copy, D2 overlap and D1.
Each clears pending low-six status bits, submits, polls only completion bit 3
with a 100 ms timer timeout, acknowledges via W1C and checks the pending bits
are cleared. The code never treats the full FD08 value as a physical signature.
Lists aren't modified or reused until completion. IRQ/FIQ stay masked.

## Explicit surface layout

`vflash_gpu_surface_t` now carries a `vflash_gpu_layout_t` field. Existing
registration initializes LINEAR; the software backend validates that property
and rejects manually supplied tiled descriptors. At this experimental milestone ZEVIO_TILED was reserved. The subsequent
[native HAL integration](gpu-native-hal.md) now supplies a constrained tiled
constructor and native operations; the limits below describe this earlier test.
Existing registration calls remain source-compatible. The experimental native
allocations are not smuggled through a linear framebuffer descriptor. Surface
registration now occupies 52 bytes on the ARM ABI; rebuild API consumers.
A public tiled constructor, ownership/busy-state handling and native fill
dispatch have since been added in the [native HAL](gpu-native-hal.md).

## Reproduce

```sh
cmake --build build --target gpu_tiled_off_a_disc_image gpu_tiled_off_b_disc_image \
  gpu_tiled_off_linear_disc_image gpu_tiled_on_a_disc_image \
  gpu_tiled_on_b_disc_image gpu_tiled_on_linear_disc_image
python3 tools/gpu_tiled_smoke.py --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --out build/gpu-tiled-acceptance
```

The runner executes six original-firmware boots without ELF injection, and
saves each boot's hashes, log, complete SDRAM dump, actual frame.ppm and
`tiled-result.json` under off-a/off-b/off-linear/on-a/on-b/on-linear. Reference
pixels are computed only on the host and never supplied to the emulator.
The common smoke runner's opt-in `--trace-video` enables existing VE logging;
no emulator changes are required. Missing completion, pattern corruption,
changed padding, wrong D1 data, missing swaps or mismatched scanout fail tests.

Source contract remains FlashEm
[`hw.c`](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/hw.c)
and [`ge.c`](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/ge.c)
at revision fb109dd01a49239c5884fcec60b9dc451fb651f8. Timing is synthetic and
correct screenshots do not validate physical cache/DMA/IRQ behavior. The
[GPU guide](gpu.md) and [inventory](zevio-registers.md) record the broader scope.
