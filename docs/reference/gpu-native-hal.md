# Restricted native GPU HAL integration

This integrates the FlashEm-verified tiled path into the general GPU API. It is
still an experimental, cache-off, identity-addressed subset, not a physical
Zevio driver or a full-screen SDL2 renderer. No software fallback is hidden in
the native backend.

## Surface and capability contracts

`vflash_gpu_surface_register_tiled_ex` borrows at least `(height / 8) * 16384`
bytes of 64-byte-aligned SDRAM with an explicitly supplied physical address for
any tile-aligned logical size (`width` a multiple of 32 in `32..1024`, `height`
a multiple of 8 in `8..1024`, with the model's 1024-pixel backing pitch).
`vflash_gpu_surface_register_tiled` preserves the legacy 64x16 (32768-byte)
convenience wrapper. CPU pointer and physical address must be equal and within
modeled SDRAM. This conservative constructor rejects arbitrary virtual aliases.
It does not perform MMU translation or prove an allocation exists: the caller
owns and guarantees that storage, its identity mapping and its lifetime. Normal
registration remains linear and unchanged. Both kinds carry an explicit layout.

Use `vflash_gpu_caps_for_layout`, not a global acceleration flag:

| Operation | Software / linear | Native / tiled |
| --- | --- | --- |
| Fill rectangle | Yes | Yes, clipped to registered view/clip |
| General blit | Yes | Unsupported, including independent allocations |
| Same-surface region copy | Use software blit | Explicit nonoverlapping D2 copy (opaque) |
| Same-surface color-key sprite | No | Yes (`vflash_gpu_draw_sprite`, `0xC8` `BGR555` color-keyed, clipped) |
| Same-surface alpha-blended sprite | No | Yes (`vflash_gpu_draw_sprite_ex` with `ABGR4444`, `caps.alpha_blend = 1`) |
| Linear-to-tiled upload | Use software blit | Yes (`D0`/`0E`/`89`/`95`), contiguous word-aligned rows |
| Direct present | Existing video API | Yes, bounded placement within 320x240 |
| Scaling, rotation, triangles | No | No |
| D1 readback through HAL | No | No; diagnostic only |

Other backend/layout combinations return UNSUPPORTED. The legacy caps query
describes default linear support; native callers must select tiled layout.
Surface registration is 52 bytes and contexts are 8 bytes on the ARM ABI.
Caller-owned context objects must not be copied or reinitialized while active.
Only one native context may own the engine at a time; software contexts are
independent. The host build rejects native initialization without touching MMIO.

D2 source/destination rectangles must be fully in bounds, fit within the model's
65536-pixel copy buffer, and the surface clip must be full. Nontrivial overlap
is rejected, even though one experimental snapshot-style overlap passed:
generalized physical overlap semantics remain unknown. No cross-base copy is
inferred from a single backing-surface model (`0xA8000098`).

`vflash_gpu_upload(gpu, dst, src, sx, sy, w, h, dx, dy)` uploads a linear
`VFLASH_BGR555` source rectangle into a native tiled destination using
`D0000002` + `0E00nnnn` + `89000000` (texture-cache sync) + `95000000` (sync).
The linear source must carry a known 32-bit-aligned identity physical address in
SDRAM and must not overlap the destination backing allocation. Because `0E`
streams 32-bit words contiguously into the `D0` rectangle (`w` pixels per row),
`w` must be even, the starting byte offset `(sy * src->stride + sx) * 2` must be
4-byte aligned, `(w * h) / 2` must fit in 16 bits (`<= 65535` words), the
destination clip must be full, and multi-row uploads (`h > 1`) require
`w == src->stride` (use `h == 1` strips when uploading sub-rectangles from a
wider linear surface).

`vflash_gpu_draw_sprite(gpu, surface, sx, sy, w, h, dx, dy)` draws a color-key
transparent sprite from `(sx, sy, w, h)` in the **same** native tiled surface to
`(dx, dy)` using `0xC8010004` (`caps.color_key_sprite = 1` for
`VFLASH_GPU_ZEVIO` + `VFLASH_GPU_LAYOUT_ZEVIO_TILED`). It emits a 13-word list
(+ `0x1400FD01` end-of-list):
`0x81000000` (origin `0,0`), `0x82` / `0x83` (clipped destination bounds),
`0x1E000000` (depth test disabled), `0x88000000` (alpha blending disabled),
`0x8600003F` (`1024x1024` 16-bit `BGR555` texture window so `u, v` coordinates
never wrap inside `0..1023`), `0x87000000` (texture base `(0,0)` in the active
surface, `16 bpp` because `pal_pending` is `0`), `0xC8010004` with `attr = 0`,
clipped destination `t<<16|l`, size `ch<<16|cw`, and clipped source UV
`v0<<16|u0`, followed by `0x95000000` (sync).

Source coordinates `(sx, sy, w, h)` must lie within `0..surface->fb.width` and
`0..surface->fb.height` (`w >= 0, h >= 0`). Destination `(dx, dy)` may be
negative or partially outside the surface/clip rectangle and is clipped against
`surface->clip_x..clip_x+clip_w` and `surface->clip_y..clip_y+clip_h` before
command emission (fully clipped or zero-size sprites return `VFLASH_GPU_OK`
without submitting a list). If the clipped source UV rectangle and clipped
destination rectangle overlap, `VFLASH_GPU_UNSUPPORTED` is returned. In
FlashEm's `c555()` (`ge.c:162-164`), **both `0x0000` and any pixel with bit 15
set (`0x8000..0xFFFF`) are treated as transparent** and leave the destination
pixel untouched, while `0x0001..0x7FFF` are written opaquely. Note that during
display scanout (`hw.c:2079`), `0x0000` is opaque black whereas `0x8000` is
transparent to the video engine's background color; destination pixels written
by `0xC8` always have bit 15 clear (`0x0001..0x7FFF`).

`vflash_gpu_draw_sprite_ex(gpu, surface, format, sx, sy, w, h, dx, dy)` extends
sprite rendering with an explicit texture format enum (`vflash_gpu_texture_format_t`):
- `VFLASH_GPU_TEXTURE_BGR555_KEY` (0): Emits `0x8600003F` (bit 8 clear). Decodes 16-bit
  `BGR555` with color-key transparency as described above. `vflash_gpu_draw_sprite`
  delegates directly to this format.
- `VFLASH_GPU_TEXTURE_ABGR4444` (1): Emits `0x8600013F` (bit 8 set, `caps.alpha_blend = 1`).
  Decodes `0xABGR` nibbles: $a = \text{word} \gg 12$. A texel with $a = 0$ is completely
  transparent and skipped. For $a > 0$, alpha weight is $al = a + 1 \in [2, 16]$ out of 16,
  blended via integer linear interpolation $\lfloor (src \cdot al + dst \cdot (16 - al)) / 16 \rfloor$.
  When $a = 15$, $al = 16$ (100% opaque); in particular, `0xF000` encodes opaque black
  ($a=15, b=0, g=0, r=0$), which overwrites destination with `0x0000`, distinguishing it
  from `BGR555_KEY` where `0x0000` is transparent.

Because every command list explicitly programs `0x86` (`0x8600003F` or `0x8600013F`),
alternating opaque (`D2`), color-keyed (`BGR555_KEY`), and translucent (`ABGR4444`)
sprites within the same frame deterministically resets the texture decoding mode without
residual state leakage. Register-driven constant blending (`0x88`/`0x8D`), emulator-synthetic
destination alpha (`abuf`), and paletted `4/8 bpp` textures (`0x8F`..`0x94`) remain unexposed.

## Initialization and ownership

Call `vflash_clock_init`, initialize/present the existing linear video buffer,
then initialize the native context. Native initialization checks ARM SVC with
IRQ/FIQ masked, low vectors and caches disabled, plus the verified timer/PMU
configuration. Keep that timer running. With the optional SDK MMU, retain the
identity profile and add the uncached/unbuffered A800 section before enabling:

```c
vflash_mmu_identity();
vflash_mmu_map_section(0xA8000000, 0xA8000000, VFLASH_MMU_PRIV_RW);
vflash_mmu_enable();
```

Check every result in real application code. Command lists, surfaces, code,
stack and display MMIO must remain identity accessible, C=B=0. Pointer equality
alone does not prove a caller's custom MMU tables have that property. Arbitrary
MMU configurations and caches are unsupported. The unchanged default MMU profile
does not automatically add accelerator MMIO.

The backend owns a static, 64-byte-aligned 64-byte command list. It submits
physical addresses, polls only completion bit 3, acknowledges the observed W1C
low-six bits, and never treats FD08 as a physical signature. Every operation is
synchronous with a 100 ms device-time timeout. On timeout or failed status ack,
the backend is poisoned: further submissions and successful shutdown/reinit are
refused, and command storage is not reused. Keep affected surface storage alive;
no reset/cancellation/recovery sequence is established. This failure branch is
implemented but has not been fault-injected in the current emulator.

Rendering into the tiled buffer currently displayed by the video driver returns
BUSY. Render the other allocation, complete operations, then present it. Do not
write scanned-out storage through CPU aliases or another context/descriptor.
Caller code must serialize operations; there is no interrupt-safe queue, thread
scheduler, buffer reference counting or asynchronous fence API.

## Presentation stays in the video driver

`vflash_gpu_present(gpu, surface, x, y)` and
`vflash_gpu_present_view(gpu, surface, w, h, x, y)` delegate to
`vflash_video_present_tiled_view` (`vflash_video_present_tiled_ex` and
`vflash_video_present_tiled` remain for full-surface and 64x16 presentation).
`vflash_gpu_present_view` presents only the top-left `w x h` tile-aligned
viewport (`x=0..w-1, y=0..h-1`) of a larger tiled surface while keeping
offscreen columns (`x >= w`) or rows (`y >= h`) in the same 1024-pixel-stride
backing allocation available as a sprite/tile atlas for `D2` copies. Placement
and viewport dimensions are bounds-checked within 320x240; tests cover 64x16 at
(13,19), (0,0), (256,224), 96x24 at (224,216), and a 96x32 viewport of a 128x32
surface at (16,24), (0,0) and (224,208). The video driver owns vblank polling,
temporary layout/window changes, the physical scanout pointer, the full
backing-allocation size guard (`(surface->fb.height / 8) * 16384` bytes) and
restoration. It requires the existing SDK-owned linear buffer/configuration,
saves its address, and restores the canonical full linear window on successful
native shutdown. It never converts or copies tiled pixels. Normal linear
present/init are rejected while tiled scanout is active. The existing
backbuffer/front-bank state remains intact.

Presentation waits for a fresh vblank with a 100 ms timeout. Shutdown also waits
for vblank before restoring linear scanout; it returns an error if restoration
cannot complete, leaving ownership intact. Callers must check the newly returned
shutdown result before releasing buffers. No unrelated firmware display setup
is captured or promised restorable.

## Complete-frame workload and verification

`examples/gpu/integrated.c` uses only the public HAL for native commands. It
first exercises a generalized 96x24 (49152-byte, three 8-row bands) tiled
surface (`frame_wide`), verifying multi-band fills, single-row and multi-row
linear-to-tiled uploads (`vflash_gpu_upload`), cross-tile D2 copies, and
presentation at (224,216) (plus active-front upload rejection). It then
alternates two 64x16 tiled allocations for 32 frames. Each frame clears the
backbuffer, draws colored regions, performs a nonoverlapping D2 region copy,
waits, presents directly and confirms writes to the active front buffer fail.
It checks unsupported blits/overlap, ownership/reinitialization rejection,
layout-conditioned caps and successful shutdown/linear recovery.

Six final-view builds preserve A at (0,0), B at (256,224), or recovered linear
scanout, with MMU off/on. The runner independently checks every tiled word across
all three tiled allocations (`frame_a`, `frame_b`, and `frame_wide`), including
untouched padding, keeps the CPU linear buffer black, verifies actual full RGB
screenshots, and checks scanout transitions (`wide -> A -> B -> linear`) and the
32 alternating A/B frames. Each run completes 213 native lists. No CPU layout
conversion occurs.

```sh
cmake --build build --target gpu_frame_off_a_disc_image gpu_frame_off_b_disc_image \
  gpu_frame_off_linear_disc_image gpu_frame_on_a_disc_image \
  gpu_frame_on_b_disc_image gpu_frame_on_linear_disc_image
python3 tools/gpu_smoke.py --test frame --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --out build/gpu-frame-acceptance
```

Evidence includes original-firmware entry traces/hashes, RAM, actual frame.ppm,
video address logs and `frame-result.json`. The ELF is used only for symbols,
not injected into guest memory. Existing software-HAL tests remain separate.

## Native sprite-animation demonstration

`examples/gpu/sprite.c` demonstrates a shared-backing sprite pipeline using two
`128x32` tiled allocations (`65536` bytes each, four 8-row bands):

- `x=0..95, y=0..31`: visible `96x32` playfield.
- `x=96..127, y=0..31`: offscreen `32x32` sprite atlas in the same 1024-wide
  tiled backing allocation.

During initialization, a `16x16` opaque multicolor pattern in linear SDRAM is
uploaded once into each surface's offscreen atlas at `(104, 8)` via
`vflash_gpu_upload` (`2` uploads total). Across 32 alternating A/B frames, each
backbuffer redraws its `96x32` background using five native `D3` fills, copies
the `16x16` sprite from `(104, 8)` into its animated playfield position using
`vflash_gpu_copy_region` (`D2`, crossing both `x=32`/`64` and `y=8`/`16`/`24`
tile boundaries), and presents only the `96x32` viewport via
`vflash_gpu_present_view`. Each run completes `194` native lists (`2` init
uploads + `32 * 6` frame lists).

```sh
cmake --build build --target gpu_sprite_off_a_disc_image gpu_sprite_off_b_disc_image \
  gpu_sprite_off_linear_disc_image gpu_sprite_on_a_disc_image \
  gpu_sprite_on_b_disc_image gpu_sprite_on_linear_disc_image
python3 tools/gpu_smoke.py --test sprite --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --out build/gpu-sprite-acceptance
```

## Native transparent-sprite diagnostic

`examples/gpu/transparent.c` and `tools/gpu_smoke.py --test transparent` verify native
`0xC8` color-keyed sprite rendering (`vflash_gpu_draw_sprite`) over the same
shared `128x32` tiled backing layout (`96x32` playfield + `32x32` offscreen
atlas):

- A `16x16` sprite asset in linear SDRAM includes transparent corner cutouts
  (`0x0000`, `0x8000`, `0xFFFF`, `0x9ABC`), a transparent `4x4` center window
  alternating `0x8000` and `0x0000`, and opaque multicolor border/diagonal/quadrant
  pixels (`0x0001..0x7FFF`).
- Uploaded once per A/B surface into `(104, 8)` via `vflash_gpu_upload` (`2`
  uploads total).
- Each of the 32 alternating frames draws the 5-band multicolor background, then
  renders the `16x16` transparent sprite twice: once unclipped at `(sx, sy)`
  (`256` pixels, exercising FlashEm's `tex_setup`/`tfetch` fast path across
  tile boundaries) and once at `(-4, 8)` clipped to `(2, 4, 20, 24)` (`160`
  clipped pixels, exercising negative destination coordinates, surface clipping,
  and FlashEm's `texel` path).
- Verifies argument/overlap/active-front rejection, exact 32768-word surface and
  offscreen-atlas preservation, `226` completed native lists (`2` uploads +
  `32 * 7` frame lists), A/B vblank scanout, and linear restoration with MMU
  off and on.

```sh
cmake --build build --target gpu_transparent_off_a_disc_image gpu_transparent_off_b_disc_image \
  gpu_transparent_off_linear_disc_image gpu_transparent_on_a_disc_image \
  gpu_transparent_on_b_disc_image gpu_transparent_on_linear_disc_image
python3 tools/gpu_smoke.py --test transparent --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --out build/gpu-transparent-acceptance
```

## Native alpha-blending diagnostic

`examples/gpu/alpha.c` and `tools/gpu_smoke.py --test alpha` validate FlashEm's modeled
alpha blending without exposing it in the public HAL (`caps.alpha_blend` remains
`0`). Using the same `128x32` tiled allocation (`96x32` visible playfield divided
into Black, Red, Green, and Blue quadrants + `32x32` offscreen atlas), it tests
both blending paths:

- **`ARGB4444` (`0x8600013F`):** Texel alpha $a = 0$ is transparent; $a = 7$
  produces 50% white; $a = 3$ produces 25% white; $a = 15$ produces 100% white.
  Linear interpolation $(src \cdot al + dst \cdot (16 - al)) / 16$ occurs
  automatically with `blend = 0`.
- **`BGR555` with Constant Blending (`0x88000002` + `0x8D808080` / `0x8D404040`):**
  $k = 128$ produces 50% white; $k = 64$ produces 25% white; `blend = 0` produces
  100% white.
- **Exact Verified Outputs:** Both paths produce bit-for-bit identical outputs:
  - 50% White: Black $\rightarrow$ `0x3DEF`, Red $\rightarrow$ `0x3DFF`,
    Green $\rightarrow$ `0x3FEF`, Blue $\rightarrow$ `0x7DEF`.
  - 25% White: Black $\rightarrow$ `0x1CE7`, Red $\rightarrow$ `0x1CFF`,
    Green $\rightarrow$ `0x1FE7`, Blue $\rightarrow$ `0x7CE7`.
  - 100% White: `0x7FFF`.
  - 0% Alpha: original background untouched (`0x0000`, `0x001F`, `0x03E0`, `0x7C00`).
- **Clipping:** Unclipped draws test the `tex_setup` fast path ($\ge 256$ pixels);
  clipped draws test the `texel` slow path ($< 256$ pixels).
- **Runs:** All six MMU off/on, A/B/linear presentation variants pass with
  `608` frame lists + `4` init uploads, exact SDRAM word comparisons, and
  direct scanout PPM verification.

```sh
cmake --build build --target gpu_alpha_off_a_disc_image gpu_alpha_off_b_disc_image \
  gpu_alpha_off_linear_disc_image gpu_alpha_on_a_disc_image \
  gpu_alpha_on_b_disc_image gpu_alpha_on_linear_disc_image
python3 tools/gpu_smoke.py --test alpha --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --out build/gpu-alpha-acceptance
```

## Validation result

All six integrated final-view cases passed with exact raw surface/padding and
scanout comparisons. Both existing software-HAL scenes, Hello World, animation
and UDF panic firmware regressions are checked separately. The host suite has
45 tests plus three ELF contracts. New guest checks cover clipped native fills,
invalid placements, unsupported linear operations, owner reinitialization,
active-front and overlapping-alias rejection, copy restrictions and shutdown.

## Timing interpretation

`vflash_gpu_stats` reports completed-list count and aggregate sampled submission
and completion-wait ticks for native contexts. Tick resolution is 100 us;
individual sub-tick operations can report zero, so aggregate sampled counters
are not high-resolution CPU profiling. The workload also records total client
operation time, vblank/presentation time and full 32-frame elapsed time.
Client operation time includes command preparation and completion polling, not
pure CPU compute. FlashEm executes GE rasterization in host code and synthesizes
completion delay; these measurements establish no physical GPU speed or gain.

Next extend dimensions only after independent full-surface verification, add
formal surface lifetimes and controlled device-reset/error tests, then consider
D1 API/readback and verified uploads. Independent-buffer blits, transparency,
cache maintenance, hardware IRQ handling and physical compatibility remain
unverified. See [the tiled investigation](gpu-tiled.md) for D1/D2 evidence and
[the original GPU guide](gpu.md) for provenance and register inventory.

### Recorded 32-frame sample

| Counter | MMU off | Identity MMU |
| --- | --- | --- |
| Completed native lists | 208 | 208 |
| Client operation time (includes completion waits) | 5.2 ms | 5.1 ms |
| Sampled submission time | 0.0 ms | 0.0 ms |
| Sampled completion waits | 4.4 ms | 4.1 ms |
| Vblank/presentation time | 514.2 ms | 526.9 ms |
| Total elapsed | 519.8 ms | 532.5 ms |

The first frame starts at an arbitrary vblank phase, so these short totals do
not establish a steady-state frame-rate or MMU overhead difference. Submission
samples below 100 us read zero, not zero cost. Per-call quantized counters must
not be subtracted to infer precise CPU-only compute time. This is a repeated
64x16 viewport workload; comparison with a 320x240 software clear is invalid.
