# Restricted native GPU HAL integration

This integrates the FlashEm-verified tiled path into the general GPU API. It is
still an experimental, cache-off, identity-addressed subset, not a physical
Zevio driver or a full-screen SDL2 renderer. No software fallback is hidden in
the native backend.

## Surface and capability contracts

`vflash_gpu_surface_register_tiled` borrows at least 32768 bytes of 64-byte-aligned
SDRAM with an explicitly supplied physical address. Initially the logical
surface is fixed at **64x16**, with the model's 1024-pixel backing pitch. CPU
pointer and physical address must be equal and within modeled SDRAM. This
conservative constructor rejects arbitrary virtual aliases. It does not perform
MMU translation or prove an allocation exists: the caller owns and guarantees
that storage, its identity mapping and its lifetime. Normal registration remains
linear and unchanged. Both kinds carry an explicit layout.

Use `vflash_gpu_caps_for_layout`, not a global acceleration flag:

| Operation | Software / linear | Native / tiled |
| --- | --- | --- |
| Fill rectangle | Yes | Yes, clipped to registered view/clip |
| General blit | Yes | Unsupported, including independent allocations |
| Same-surface region copy | Use software blit | Explicit nonoverlapping D2 copy |
| Direct present | Existing video API | Yes, bounded placement within 320x240 |
| Alpha, scaling, rotation, triangles | No | No |
| D1 readback through HAL | No | No; diagnostic only |

Other backend/layout combinations return UNSUPPORTED. The legacy caps query
describes default linear support; native callers must select tiled layout.
Surface registration is 52 bytes and contexts are 8 bytes on the ARM ABI.
Caller-owned context objects must not be copied or reinitialized while active.
Only one native context may own the engine at a time; software contexts are
independent. The host build rejects native initialization without touching MMIO.

D2 source/destination rectangles must be fully in bounds and the surface clip
must be full. Nontrivial overlap is rejected, even though one experimental
snapshot-style overlap passed: generalized physical overlap semantics remain
unknown. No cross-base copy is inferred from a single backing-surface model.

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

`vflash_gpu_present(gpu, surface, x, y)` delegates to
`vflash_video_present_tiled`. Placement is bounds-checked; tests cover (13,19),
(0,0) and (256,224). The video driver owns vblank polling, temporary layout/window
changes, the physical scanout pointer and restoration. It requires the existing
SDK-owned linear buffer/configuration, saves its address, and restores the
canonical full linear window on successful native shutdown. It never converts
or copies tiled pixels. Normal linear present/init are rejected while tiled
scanout is active. The existing backbuffer/front-bank state remains intact.

Presentation waits for a fresh vblank with a 100 ms timeout. Shutdown also waits
for vblank before restoring linear scanout; it returns an error if restoration
cannot complete, leaving ownership intact. Callers must check the newly returned
shutdown result before releasing buffers. No unrelated firmware display setup
is captured or promised restorable.

## Complete-frame workload and verification

`examples/gpu/integrated.c` uses only the public HAL for native commands. It
alternates two tiled allocations for 32 frames. Each frame clears the 64x16
backbuffer, draws colored regions, performs a nonoverlapping D2 region copy,
waits, presents directly and confirms writes to the active front buffer fail.
It checks unsupported blits/overlap, ownership/reinitialization rejection,
layout-conditioned caps and successful shutdown/linear recovery. The scene is
a complete frame of the restricted **64x16 view**, not a 320x240 GPU benchmark.

Six final-view builds preserve A at (0,0), B at (256,224), or recovered linear
scanout, with MMU off/on. The runner independently checks every tiled word,
including untouched padding, keeps the CPU linear buffer black, verifies actual
full RGB screenshots, and checks the first 32 scanout addresses alternate A/B.
Each frame run completes 208 native lists. No CPU layout conversion occurs.

```sh
cmake --build build --target gpu_frame_off_a_disc_image gpu_frame_off_b_disc_image \
  gpu_frame_off_linear_disc_image gpu_frame_on_a_disc_image \
  gpu_frame_on_b_disc_image gpu_frame_on_linear_disc_image
python3 tools/gpu_frame_smoke.py --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' --out build/gpu-frame-acceptance
```

Evidence includes original-firmware entry traces/hashes, RAM, actual frame.ppm,
video address logs and `frame-result.json`. The ELF is used only for symbols,
not injected into guest memory. Existing software-HAL tests remain separate.

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
