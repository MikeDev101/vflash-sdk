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
