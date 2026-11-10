# GPU HAL and Zevio investigation

This HAL provides synchronous software fills and raw surface blits. Separate
experiments also submit actual command lists to FlashEm's emulated graphics
engine. It does not establish physical Zevio compatibility, a complete native
GPU driver, or a hardware SDL2 renderer. Existing video and panic drivers are
unchanged. No SDL2 implementation currently exists in this checkout.

The current [restricted native HAL](gpu-native-hal.md) now integrates tile-aligned
tiled fills, linear-to-tiled uploads (`D0`/`0E`), nonoverlapping same-surface D2
copies and direct presentation.
The original one-tile native experiment and its measurements below remain
separate historical research workloads, not the integrated frame benchmark.

## Public API and capability boundary

`include/vflash/gpu.h` defines caller-owned contexts and surface registrations.
`src/gpu/gpu.c` validates/dispatches; `gpu_software.c` implements fills/blits;
`gpu_zevio.c` now provides the restricted ARM native backend; host builds
return UNSUPPORTED without MMIO.
An invalid/failed initialization leaves the context inactive. Shutdown does not
free pixel memory: registration borrows storage and never allocates or copies it.
Keep registered storage alive and writable for the duration of operations.
Surface structs are caller-owned descriptors; don't modify them during calls.

```c
#include "vflash/gpu.h"
vflash_gpu_t gpu;
vflash_gpu_surface_t target;
/* fb comes from vflash_video_init or vflash_video_backbuffer. */
vflash_gpu_init(&gpu, VFLASH_GPU_SOFTWARE);
vflash_gpu_surface_register(&target, &fb, 0, 0);
vflash_gpu_fill_rect(&gpu, &target, 10, 10, 40, 30, 0xFF0000);
vflash_gpu_wait_idle(&gpu);
/* Check returned results in application code before presenting. */
```

The legacy/default linear capability query reports software fill/blit only.
Use `vflash_gpu_caps_for_layout` for native tiled fill/present/same-surface-copy
capabilities. Alpha, scaling, rotation and triangles remain unsupported. Unsupported
hardware requests never silently fall back to CPU rendering. The original native.c fill remains a separate diagnostic; native HAL support
is bounded by the [integration contract](gpu-native-hal.md).

Surfaces use existing `vflash_framebuffer_t`: xBGR1555, 16 bits, stride and
capacity in pixels, opaque color conversion through `draw_color`. Fill reuses
`draw_rect`; unset text/background concerns remain in the existing drawing API.
Blit copies raw 16-bit words, including bit 15; it does not alpha-composite or
interpret transparency. The destination clip rectangle applies to both
operations; source clip does not crop blit. Source dimensions define the copy.
Empty rectangles are no-ops, negative sizes invalid. Coordinates widen before
arithmetic. Exact-base equal-stride self-blits have memmove semantics. Other
partially overlapping registrations are rejected before writing. Padding is
preserved. Software calls complete synchronously; wait-idle is immediate.

CPU pointer and optional physical-address metadata are separate. Registration
neither derives a bus address nor validates hardware eligibility. The software
backend only uses the CPU pointer. Before any future device submission, validate
physical SDRAM bounds, layout, alignment and cache maintenance independently.
An arbitrary MMU alias cannot be supplied as a device address. The current
native experiment uses MMU-off, cache-off, 64-byte-aligned identity buffers.
MMU software tests use CPU pointers only and require no new device mappings.
The optional default MMU profile does not map A800; native HAL clients must
explicitly add that section. Integrated identity-MMU execution is tested.

## Display and accelerator are distinct

The existing B800 video engine scans a linker-owned linear 320x240 buffer,
640-byte pitch, with an optional second 153600-byte bank. It handles scanout,
layer composition, and vblank; `vflash_video_present` still owns presentation.
Animation polls a fresh W1C vblank before flipping the owned address; it never
submits a graphics command. See [video](video.md) and [animation](animation.md).

A800 is the separate command engine in the pinned emulator. Its old `lcd`
identifiers and comments are misleading: the current implementation actually
calls `ge_run`, not just a register stub. The native destination uses a different
1024-wide tiled address layout. Do not program A800 with a linear framebuffer.
The [register inventory](zevio-registers.md) gives evidence and confidence.

## Native experiment and firmware evidence

`examples/gpu/native.c` initializes a dedicated 16384-byte tiled region to
0x255A before submission. A short list resets depth-fill mode, sets clip,
issues D3000003 with p=0, size=8<<16|32, color=0x001F, then 1400FD01 list-end.
It writes the physical list/destination addresses to A800, starts the list,
and polls status bit 3 with a 100 ms device-time timeout. All CPU destination
writes finish before submission. After completion the CPU only reads that
region, checks all 8192 words, then explicitly copies the resulting one tile
into the linear display buffer. That presentation copy is CPU work, not GPU
scanout acceleration. The experiment acknowledges completion and keeps IRQ/FIQ
masked. No firmware graphics service is called and no old scheduler resumes.

Actual native-run log also shows firmware PCs 100117C0/100117CC writing
A800+00/+10 and PCs 10011808/10011814/10011820 setting +90/+94 to 00F00200 and
+98 to 10800000. The application then writes its own +90=00000200 and +98
buffer address. These are observed writes, not proof of the low bits' complete
hardware meaning. They corroborate the already modeled firmware initialization.
No firmware code or commercial assets are included in sources or tests.

## FlashEm rendering fidelity and instrumentation

Audited revision: `fb109dd01a49239c5884fcec60b9dc451fb651f8`.
`hw.c:lcd_run_list` decodes an ARM-submitted physical command address and calls
`ge.c:ge_run`; GE accesses guest SDRAM directly. Commands implement fills,
upload/download/copy, sprites and model/triangle paths. Host software rasterizes
these operations into guest memory. SDL presents the B800-composed framebuffer;
this path is not a host SDL substitute for the SDK's API calls.

It is nevertheless an approximate graphics implementation. GE maintains host
side depth and destination-alpha arrays whose real storage is unknown; culling,
float conversion, filtering, near clipping, lighting and sprite depth have
experiment switches and game-trace-derived assumptions. Some opcodes are skipped
or counted as unknown. Completion is synthesized after 20 us plus a pixel-count
term, not a modeled pipeline, and fill statistics don't establish real GPU speed.
Comments claiming lists are not executed predate current code and are stale.
Neither screenshots nor passing this test validate all commercial streams.

Existing optional instrumentation is sufficient; no emulator patch was needed:
VFLASH_LCDLOG (list words), VFLASH_GELOG (statistics/unknown commands),
VFLASH_GETRACE (frame-filtered commands), VFLASH_GECAP (RAM/list replay capture),
VFLASH_GEWATCH (one pixel), VFLASH_SURFLOG (surface changes), VFLASH_VELOG
(scanout), VFLASH_DMALOG and the MMIO histogram. Keep captures in ignored build
folders; RAM captures may contain private firmware/game data. The SDK smoke
runner scrubs inherited VFLASH variables for reproducibility. Use a separate
isolated emulator run when enabling these options; don't alter normal rendering
or feed expected pixels into the guest. No per-pixel logging is enabled by HAL.

## Reproduce and measured results

```sh
cmake --build build --target gpu_software_disc_image gpu_mmu_disc_image gpu_native_disc_image
ctest --test-dir build --output-on-failure
python3 tools/gpu_smoke.py --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --test software --out build/gpu-software-acceptance
python3 tools/gpu_smoke.py --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --test mmu --out build/gpu-mmu-acceptance
python3 tools/gpu_smoke.py --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --test native --out build/gpu-native-acceptance
```

To view the native result interactively after building, use the local emulator
with the ROM and generated CUE (run from the repository root):

```sh
FLASHEM_BIOS="$PWD/dumps/A29L160ATV@TSOP48.BIN" \
  build/emulator/flashem --language bios "$PWD/build/gpu/native/homebrew.cue"
```

Expect a small red 32x8 block at the top-left on black, copied from the GE's
one-tile output. This deliberately small operation is not a full-screen demo.
The original root launchers retain their existing demo choices.

The software image exercises clear, rectangle, blit and destination clipping.
All native RAM pixels and actual RGB scanout match an independent operation
oracle. The native case compares the first 256 tiled words with the actual software
HAL compiled on the host, verifies they are red, and checks all remaining
7936 words retain their sentinel, completion is FD08, and the displayed linear
copy matches. `gpu-result.json` preserves counters; `result.json` retains boot
traces/hashes, with actual `frame.ppm`, RAM and run log beside it.

Initial device-timer measurements, 100 us resolution:

| Workload (8 calls per software batch) | MMU off | MMU identity |
| --- | --- | --- |
| 320x240 black clear | 49.6 ms | 49.6 ms |
| 100x60 red fill | 3.9 ms | 4.0 ms |
| 8x8 raw blit | 0.2 ms | 0.1 ms |

These are aggregate CPU/completion times for synchronous software, not per-call
latencies or physical performance. New diagnostic scenes present once, so HAL
FPS is not measured. The native 32x8 fill's submission and completion-wait
samples both read zero ticks: below timer resolution, not zero-time hardware.
Different workloads/layouts and synthesized completion prohibit a speedup claim.
All 45 host tests and three ELF checks passed. Existing firmware-booted
Hello World, double-buffered animation and genuine UDF panic regressions also
passed. Both software HAL modes matched the independent full-frame oracle.

Software framebuffer is borrowed (153600 bytes), sprite 128 bytes, no heap;
native adds 16384 bytes and a 32-byte list. The ARM build uses an 8-byte context and a 52-byte surface registration;
use the ELF map for placement/alignment padding. Existing
panic allocations are independent. Full-screen clears and existing double
buffered animation remain CPU-rendered.

## SDL2 plan and next steps

RenderClear/FillRect map to software fills, unscaled RenderCopy to raw blit only
for compatible opaque formats. Texture updates need explicit conversion and
ownership. RenderCopyEx, blend modes, scaled copies and triangles remain
unsupported/software extension work. Render targets may borrow registered
surfaces; present must continue through the existing video driver. Do not add
an intermediate framebuffer unless a required format/layout conversion warrants
it. No SDL2 code was modified and no custom SDL renderer is delivered.

Next extend the [restricted native HAL](gpu-native-hal.md) beyond its verified
64x16 surfaces, validate larger copies/uploads and fault-inject completion/error
handling before broadening ownership or capability claims.
Hardware alpha, DMA coherency, documented IRQ masking, resets, bus arbitration,
cache-enabled execution and arbitrary nonidentity native MMU use remain unverified.

## Follow-up: direct tiled presentation

The [tiled experiment](gpu-tiled.md) now validates boundary-crossing patterns,
A/B direct tiled scanout, linear recovery, D2 region copies/one overlap case,
and D1 readback with MMU off and on. It removes CPU conversion for that
experimental presentation path. The initial native.c one-tile benchmark above
still performs its explicitly documented CPU presentation copy. Restricted Zevio HAL initialization and tiled-surface registration are now
available; see the [native HAL guide](gpu-native-hal.md) for current limits. D2 does not supply independent surface bases.
