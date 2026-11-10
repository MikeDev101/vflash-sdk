# Zevio graphics register inventory and provenance

All register names below are provisional descriptions from the pinned FlashEm
model, not manufacturer register names. Access widths listed are the tested
32-bit CPU accesses. Confidence is high only for behavior in that revision;
physical hardware is untested. No public register/command programming manual was
identified in the reviewed sources. Unlisted offsets remain unknown/stored/stubbed.

| Address | Provisional purpose | Access | Width | Modeled behavior | Source / confidence | Emulated |
| --- | --- | --- | --- | --- | --- | --- |
| A8000000 | List start | RW | 32 | Bit 0 calls ge_run at stored list address | hw.c lcd_write; native test / high model | Yes |
| A8000010 | Firmware control/mask candidate | RW | 32 | Stored; firmware writes FFF; complete hardware semantics unknown | Firmware trace / provisional | Partial |
| A8000014 | Completion/ack | RW | 32 | FD08 when done; writing low six set bits clears corresponding status; IRQ 8 | hw.c lcd_int_check, native test / high model | Approximate timing |
| A8000028 | List physical address | RW | 32 | Input to ge_run from guest SDRAM | lcd_run_list / high model | Yes |
| A8000090 | Surface top/extent candidate | RW | 32 | High 12 bits supply top row; low-field meaning not established | Firmware and hw.c / partial | Partial |
| A8000094 | Extent candidate | RW | 32 | Stored; same firmware initial value as +90 | Firmware / provisional | Stored |
| A8000098 | Surface physical address at top row | RW | 32 | Model subtracts (top>>3)*16384 for row-zero base | hw.c / high model | Yes |
| B8000000 | Video status | RW | 32 | Bit 1 vblank, W1C | ve_write/ve_vblank, existing animation / high model | Yes |
| B8000004 | Video IRQ mask | RW | 32 | Existing driver writes 0 | ve_int_check / high model | Yes |
| B800000C | Video output extent | RW | 32 | Inclusive output width/height | Existing full-pixel tests / high model | Yes |
| B8000100 | Layer enables | RW | 32 | 10 selects only color layer 0 | ve_render / high model | Yes |
| B8000108 | Layer layout | RW | 32 | Layer-0 bit 2 selects tiled instead of linear | ve_render / high model | Partial formats |
| B800010C | Background | RW | 32 | Driver sets black fallback | Existing video / model | Yes |
| B8000130 / 134 | Color-layer-0 window | RW | 32 | Inclusive start/end; width determines pitch | Existing video / high model | Yes |
| B8000160 | Color-layer-0 physical address | RW | 32 | Guest SDRAM scanout | Existing video / high model | Yes |
| BC000000 region | General DMA | RW | 32 | hw.c dmac_run copies physical memory/device words | Source / model; not used by HAL | Partial |
| DC000000 region | Interrupt controller | RW | 32 | Engine IRQ 8; video IRQ separate | Source / model; IRQs masked in tests | Partial |
| A4000000 region | Math/conversion unit | RW | 32 | Fixed/engine-float conversions modeled | Source / model; unused by HAL | Partial |

Native destination addressing in the model is
`base + (y>>3)*16384 + (x>>5)*512 + (y&7)*64 + (x&31)*2`.
Despite older comments describing 8x8 tiles, this expression groups 32x8 pixels
per 512-byte unit. The initial diagnostic uses one such unit; the [tiled follow-up](gpu-tiled.md)
checks neighboring units, direct scanout and all unused allocation words. No general physical tiling specification follows.

## Modeled transparency, sprite, and blending behavior (`ge.c` / `hw.c`)

Source inspection of FlashEm (`fb109dd01a49239c5884fcec60b9dc451fb651f8`)
establishes the following transparency and blending paths:

| Command / register | Modeled behavior | Source reference | HAL status |
| --- | --- | --- | --- |
| `0xD2000003` (`sy<<16\|sx`, `h<<16\|w`, `dy<<16\|dx`) | Unconditional raw 16-bit copy within the active surface (`*d = tmp[...]`); ignores bit 15, `0x0000`, and `blend` state | `ge.c:949-964` | Supported via `vflash_gpu_copy_region` (opaque only) |
| `0xC8ffnnnn` (`sprite`, `f=0x01` textured, `f=0x04` tint word) | Draws an axis-aligned sprite (`attr`, `y<<16\|x` signed from `0x81` origin, `h<<16\|w`, `[v0<<16\|u0]`, `[0x00BBGGRR]`) clipped to `0x82`/`0x83`; skips texels where `tex_get() == 0` | `ge.c:226-292`, `1013-1017` | Supported for 16-bit color-keyed sprites via `vflash_gpu_draw_sprite` (`0xC8010004`) |
| `0x81yyyxxx` | Sets drawing origin `(org_x, org_y)` added to `0xC8` signed 16-bit `(x, y)` | `ge.c:238`, `1002` | Issued as `0x81000000` by `vflash_gpu_draw_sprite` |
| `0x82yyyxxx` / `0x83yyyxxx` | Inclusive clip rectangle `(clip_x0, clip_y0)..(clip_x1, clip_y1)`; `fill()` and `sprite()` clip against this window | `ge.c:121-126`, `246-248`, `1003-1004` | Used by `vflash_gpu_fill_rect` and `vflash_gpu_draw_sprite` |
| `0x1E000000 \| m` | Bit 15 (`0x8000`) enables depth testing (`zenable`). Note: `sprite()` still writes `sz` to host `zbuf` when `sdepth` is active | `ge.c:103-106`, `271-275`, `982-984` | Issued as `0x1E000000` (`zenable = 0`) by `vflash_gpu_draw_sprite` |
| `0x86000000 \| fmt` | Texture format/dimensions: `w = 8 << (fmt & 7)`, `h = 8 << ((fmt >> 3) & 7)` (UVs wrap modulo `w, h`); bit 8 (`0x100`) selects `ARGB4444` instead of `BGR555` | `ge.c:162-222`, `476-527`, `1005` | Issued as `0x8600003F` (`1024x1024` 16-bit `BGR555`) by `vflash_gpu_draw_sprite` |
| `0x87yyyxxx` | Texture base `(tex_x, tex_y)` in the **same** active tiled surface (`g->surface`); sets `tex_bpp = 16` when `pal_pending == 0` | `ge.c:478-485`, `1006-1007` | Issued as `0x87000000` with `u0=sx, v0=sy` by `vflash_gpu_draw_sprite` |
| `0x8F`..`0x94` (`0xPPyyyxxx`) | Sets palette row `(pal_x, pal_y)` in the tiled surface and arms `pal_pending = 8` (`0x8F`..`0x91`) or `4` (`0x92`..`0x94`) for the next `0x87` | `ge.c:205-220`, `1008-1012` | Not exposed by HAL |
| `0x88000000 \| m` / `0x8D000000 \| k` | `(blend & 3) == 2` enables alpha blending weighted by `blend_k` (`0xBBGGRR`) and texel alpha (`1..16`); `(blend & 0x43) == 0x42` uses host `abuf` destination alpha | `ge.c:81-101`, `166-197`, `261-288`, `911`, `967` | Disabled (`0x88000000`) in `vflash_gpu_draw_sprite`; `caps.alpha_blend` remains `0` |
| `0x95000000` | Sprite / upload sync command following `0xC8` or `0x0E` | `ge.c:916-918` | Issued after `0x0E` and `0xC8` |
| `B8000100`..`B8000174` (`ve_render`) | Display scanout treats pixel bit 15 (`c & 0x8000`) as layer transparency (exposing `B800010C` background on layer 0), while `0x0000` scans out as opaque black | `hw.c:2079`, `2101` | Existing video driver scans out layer 0 over black fallback |

Key 16-bit `BGR555` color-key rule in `ge.c:162-164` (`c555`):
`c555(uint16_t p)` returns `p && !(p & 0x8000) ? p | 0x100000u : 0` in both the
per-texel path (`texel()`, used when clipped sprite area `< 256` pixels) and the
fast cached path (`tex_setup()` / `tfetch()`, used when clipped area `>= 256`
pixels). Consequently, **both `0x0000` and any word with bit 15 set
(`0x8000..0xFFFF`) are transparent** during `0xC8` `BGR555` sprite rendering and
leave the destination pixel untouched, whereas `0x0001..0x7FFF` are drawn
opaquely when `0x88000000` is active. To draw opaque dark-black sprite pixels in
`0xC8` without triggering `0x0000` transparency, use a non-zero near-black value
such as `0x0001` or `0x0421`, and use `0x8000` (or `0x0000`) for transparent
cutouts.

## Architectural evidence versus implementation observations

A first-party [ACCESS/Koto/HI platform announcement](https://www.access-company.com/en/news_event/archives/2006/010206/)
credits Koto and LSI with the Zevio 3D core, alongside µMore and MascotCapsule.
It corroborates the development partnership, not a register or timing contract.


[MAME's driver](https://github.com/mamedev/mame/blob/master/src/mame/vtech/vsmilepro.cpp)
is a skeleton, identifying ARM926EJ-S, ZSP400 and reported 2D/3D processors.
It maps only ROM and does not provide a graphics register implementation. Its
block list alone does not prove two independently programmable accelerators.

Tom Halfhill's June 12, 2006 Microprocessor Report article,
[LSI Logic Wants Your SoC](https://halfhill.com/mpr/2006/202401.pdf), describes
a Koto/LSI graphics core with geometry and rendering stages and 16-bit floating
point. Its diagrams describe command/bus interfaces, transform/lighting and
raster/depth/texture/alpha stages, not a register-level V.Flash programming ABI.
The PDF endpoint could not be fetched in this session; indexed article text and
figure captions were available. This supports a combined 2D/3D core description,
not a proven independent pair of MMIO engines. Exact on-chip partitioning remains
unresolved. Sound's marketing phrase 2D/3D MIDI must not be mistaken for graphics.
No authoritative public register map or applicable patent-derived programming
interface was established; do not invent one from high-level block diagrams.

The implementation contract tested here is pinned source:
[hw.c](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/hw.c),
[ge.c](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/ge.c),
[ge.h](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/ge.h).
Commercial traces informed upstream decoding. The SDK adds only original
command construction and synthetic pixel tests, with no proprietary fixtures.
See [GPU HAL, fidelity audit and measured results](gpu.md).
