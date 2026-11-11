# CPU-rendered Hello World in FlashEm

The evaluation target for this milestone is FlashEm's documented/implemented
behavior at commit `fb109dd01a49239c5884fcec60b9dc451fb651f8`. Physical hardware
testing is outside the current scope and compatibility remains unverified.

The firmware-booted disc displays white `Hello, World!` text above one
white-bordered rectangular bar with equal red, green and blue sections on a
dark background. All pixel values, including text and border, are computed by
the ARM CPU. There are no graphics-engine commands, sprites, textures, GPU
fills, firmware text calls, floating point, allocation or external graphics
libraries in the ARM target.

## Display path and source evidence

The path is the **custom ZEVIO video engine**, modeled at `0xB8000000`, using
linear color layer 0. It is not configured as a PL111 controller. The emulator
has an older `lcd` variable/function name for the separate `0xA8000000` graphics
command engine; that naming does not make it the framebuffer scanout controller.
Our application never writes the A800 engine.

| Finding | Evidence | Confidence for the pinned emulator |
| --- | --- | --- |
| Video engine at B800 | `VE_BASE`, `mmio_rd`, `mmio_wr` in [hw.c](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/hw.c) | High: source routing and application writes observed |
| Linear color layer 0 | `ve_render`, particularly the `j == 0` RGB path and `tiled` selection near lines 2040–2080 | High: complete captured output matches |
| xBGR1555 | `rgb555` near line 1983 and the bit-15 transparency test in `ve_render` | High: primary colors and all pixels verified |
| Width/height/window interpretation | `ve_render` reads `+0x0C`, `+0x130`, `+0x134` | High: actual 320×240 scanout captured |
| Per-frame presentation | `ve_vblank` invokes `ve_render` | High for static output; no double-buffer/tearing guarantees |
| Screenshot is actual scanout | FlashEm `src/main.c` headless screenshot block gets `vflash_get_framebuffer`, then exports its RGB pixels | High: not an independently drawn screenshot |
| MMU/caches off at handoff | Local ROM loader disassembly and entry trace; see [firmware.md](../research/firmware.md) | High for tested ROM/emulator combination |

The emulator's own focused `tools/test_ve_compositing.c` also exercises linear
RGB surfaces and opacity. The source is the contract used here; these confidence
levels do not imply physical-register validation.

## Pixel format and pitch

Configured size: **320×240**. Pitch: **320 pixels / 640 bytes** per row.
Storage: **153,600 bytes**, little endian, 16 bits per pixel:

```text
15       14..10       9..5        4..0
clear    blue        green       red
```

Bit 15 means transparent when set, so every rendered pixel leaves it clear.
Native primary values are red `0x001F`, green `0x03E0`, blue `0x7C00`, white
`0x7FFF`. This is not RGB565. Drawing APIs accept `0x00RRGGBB`, truncate each
channel to five bits, and store the native word.

The linear scanout code derives pitch from the color window's width. There is
no independent pitch register used by this path. The drawing API supports
padded host strides, but `vflash_video_present` rejects a presentation buffer
whose stride differs from 320.

## Explicit application configuration

`video.c` uses volatile 32-bit MMIO accesses. Initialization disables all
layers, configures our window/buffer, and leaves scanout disabled until drawing
has finished:

| B800 offset | Value | Purpose in this emulator |
| --- | --- | --- |
| `0x100` | `0` initially, `0x10` on present | Disable layers, then enable only color layer 0 |
| `0x004` | `0` | Disable video IRQ mask; the application uses no interrupts |
| `0x00C` | `0x00EF013F` | Output extent: `(240−1)<<16 \| (320−1)` |
| `0x108` | `0` | Clear layer-0 tiled-layout bit 2; select linear storage |
| `0x10C` | `0` | Black fallback background; opaque CPU pixels cover it |
| `0x130` | `0` | Color-layer-0 window start |
| `0x134` | `0x00EF013F` | Color-layer-0 inclusive window end |
| `0x160` | Linker address of `video_pixels` | Physical CPU-rendered framebuffer |

Readback validates configuration, while the smoke test separately validates
operation through the actual output pixels. The static image requires no
interrupt, polling loop, DMA submission, or waiting for a particular vblank:
after enable, the emulator reads the completed buffer at each `ve_vblank`.
A compiler memory barrier precedes the final layer-enable write. MMU/caches
remain off; no virtual/physical translation or cache-clean service is needed
for this model. The framebuffer never changes after presentation.

The ROM originally leaves a different state: output extent `0x00EF015F`
(352×240), `+0x100 = 0x0A004000`, `+0x108 = 4`, layer-0 address `0x10800000`
and layer-1 address `0x10BC8000`. We replace the relevant scanout state and do
not reuse either firmware buffer. Unrelated output timing/control registers
are inherited because the pinned emulator does not consult them for this
linear scanout path. No claim of complete analog display initialization is
made or required by the current emulator evaluation scope.

Local firmware disassembly corroborates the B800 register family: runtime
`0x1008CE20..0x1008CE3C` sets the layer-0 address; `0x1008CE64..0x1008CECC`
sets layer-1 plane addresses; `0x1008D060..0x1008D0B8` packs/writes the output
extent. ROM code-to-file mapping is documented in `firmware.md`. These are
evidence locations, not callable APIs used by our program.

## Memory ownership

`video_pixels` is a 64-byte-aligned array in the application's `.bss`, assigned
by the linker rather than a fixed borrowed firmware address. Resolve its
address and `__image_end` from the current ELF. Normal BSS is followed by the
separate `.panic` region, then the future heap and reserved 16 KiB application
stack. Linker assertions and ELF/packer checks enforce the SDRAM bounds.

The array is NOLOAD state: it adds RAM use, not 150 KiB of disc data. Startup
clears `.bss` and the CPU renderer then writes all 76,800 pixels. The loader
stack and inherited firmware buffers are elsewhere; the payload takes over
execution with interrupts masked and never resumes old firmware tasks.

`draw.c` checks dimensions, format, pitch and declared capacity before writing.
Rectangles widen coordinate arithmetic before clipping, so extreme signed
coordinates do not overflow. Glyph backgrounds are transparent, unsupported
characters use `?`, and scaling is limited to 1–16. The [font](fonts.md) covers all 95 printable ASCII characters, with a
seven-byte bitmap per character.

## Reproduction and acceptance checks

```sh
cmake --build build --target all disc_image
ctest --test-dir build --output-on-failure
python3 tools/host_renderer.py build/render-reference
python3 tools/emulator_smoke.py \
  --emulator /path/to/flashem \
  --bios '/path/to/A29L160ATV@TSOP48.BIN' \
  --disc build/homebrew.cue --elf build/vflash_disc_probe.elf \
  --out build/video-smoke --verify-video
```

Host tests/preview compile **the same `draw.c`, `font.c`, and `demo.c`** with
native `cc`, or the executable in `VFLASH_HOST_CC`. Python provides buffers,
canaries, comparison, and native-pixel decoding; it does not redraw the demo.
Tests exercise padded pitch, color channels, glyph bits/backgrounds, clipping,
invalid descriptors, extreme/random coordinates and deterministic output.

The firmware acceptance test uses no `--load` or ELF injection. It requires:

1. BOOT-stub and startup traces, plus the three original marker/data words.
2. `video_status = 0x56494431` and `render_status = 0x44524157`.
3. Every native framebuffer byte in the RAM dump matching the host C renderer.
4. Every RGB byte and both dimensions in FlashEm's actual screenshot matching
   that same expected image.
5. No unmodeled MMIO accesses originating in our executable. The baseline ROM
   has three known `[HW?]` writes from ROM PCs, also present before this change;
   they are not silently presented as a warning-free firmware run.

The verified reference hashes are:

```text
native BGR555: b4abcb5dd7b4a47931117345b4a6c1e1605cfa70ac12eb59cc2df3876d2f9ac2
presented RGB: 9bd9521bd5881277e51281a0e2658d22779a51c27981827597169bf8c70b34af
```

`build/video-smoke/frame.ppm` is the actual emulator capture;
`frame.bgr555` is the RAM framebuffer; `reference/` contains host-generated
comparison artifacts; `result.json` records hashes and checks. The captured
output was visually inspected as well. Evaluation is entirely through the
emulator; physical hardware access is not a prerequisite for this milestone.

The original native graphical run was also confirmed by the user. Historical
capture details, build sizes and address snapshots are in the
[graphical boot report](../history/graphical-boot.md); they are not fixed
addresses or current test counts. Build/run commands and source references
above describe the current implementation.
