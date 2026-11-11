# Native Upstream SDL2 Port and Full-Resolution Zevio Rendering

> [!NOTE]
> **FlashEm Validation Disclaimer:**
> All Zevio GE / Video Engine hardware-accelerated rendering behaviors, double-buffered scanout flipping, tile-band layouts, and timing contracts documented here are validated against the cycle-accurate **FlashEm** VTech V.Flash emulator. Real physical console timing and electrical characteristics may exhibit minor variations.

The V.Flash SDK includes a freestanding, native port of **upstream SDL2** (Git submodule `external/SDL2`, pinned to branch `SDL2`, commit `cee83f2d9a916e9de30c5712e798541fdc134b06`). The upstream tree remains 100% pristine and unmodified; all platform-specific video, rendering, runtime, and memory adaptations reside in `src/graphics/sdl2_vflash/` and `include/sdl2_vflash/`.

---

## 1. Submodule & Freestanding Build Architecture

### Pinned Upstream Commit
- **Repository:** `https://github.com/libsdl-org/SDL.git`
- **Submodule Path:** `external/SDL2`
- **Branch:** `SDL2`
- **Commit:** `cee83f2d9a916e9de30c5712e798541fdc134b06`
- **Working Tree:** Pristine, no local modifications.

### Compiler & Architecture Flags
The library (`libSDL2.a`) compiles under `arm-none-eabi-gcc` for ARM926EJ-S (ARMv5TEJ) with no hosted C library (`-nostdlib -ffreestanding`):
```text
-mcpu=arm926ej-s -marm -mfloat-abi=soft -mlittle-endian -mabi=aapcs -fno-short-enums
-Os -g3 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections
-fno-unwind-tables -fno-asynchronous-unwind-tables -DDYNAPI_NEEDS_DLOPEN=1
```

- **DynAPI Bypass:** Compiling with `-DDYNAPI_NEEDS_DLOPEN=1` forces upstream `SDL_dynapi.h` to disable `SDL_DYNAMIC_API 0`, exporting direct standard `SDL_*` entry points without requiring `dlopen` or modifying upstream files.
- **Enum ABI:** The flag `-fno-short-enums` guarantees that all enum types are 32-bit integers, satisfying upstream SDL2's `sizeof(SDL_DUMMY_ENUM) == sizeof(int)` contract.

---

## 2. Platform Driver & Driver Table Aliasing

SDL2 dynamically dispatches to video and render drivers via static driver lookup tables. Rather than patching upstream `SDL_video.c` and `SDL_render.c`, the V.Flash port activates built-in slots via preprocessor macros in `include/sdl2_vflash/SDL_config.h`:

```c
/* Video Driver: Alias DUMMY bootstrap to VFLASH */
#define SDL_VIDEO_DRIVER_DUMMY 1
#define DUMMY_bootstrap VFLASH_bootstrap

/* Accelerated Render Driver: Alias PSP driver slot to VFLASH */
#define SDL_VIDEO_RENDER_PSP 1
#define PSP_RenderDriver VFLASH_RenderDriver
#define SDL_VIDEO_RENDER_SW 1
```

- **Video Driver (`src/graphics/sdl2_vflash/SDL_vflashvideo.c`):**
  Registers the 320×240 16-bpp display mode, creates and destroys the single fullscreen window, and coordinates clean linear scanout restoration on shutdown.
- **Accelerated Render Driver (`src/graphics/sdl2_vflash/SDL_vflashrender.c`):**
  Identifies as driver `"zevio"` with `SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC`. Implements command queue hooks (`QueueFillRects`, `QueueCopy`, `RunCommandQueue`, `RenderPresent`) driving the underlying native `vflash_renderer_t`.

---

## 3. Full 320×240 Zevio Surface Geometry

The native Zevio 1020 2D engine operates in 16-pixel × 8-line tiles arranged in horizontal bands of 1024 pixels (stride = 1024 pixels, or 16,384 bytes per 8-line band).

To provide both a **full 320×240 display viewport** and an ample **320×240 texture atlas** with zero wasted SDRAM:
- **Total Surface Width:** 640 pixels
- **Total Surface Height:** 240 pixels (30 tile bands)
- **Playfield Viewport:** 320×240 at coordinate $(0, 0)$
- **Texture Atlas:** 320×240 at coordinate $(320, 0)$
- **Buffer Stride:** 1024 pixels ($16,384$ bytes per band)
- **Buffer Size:** $(240 / 8) \times 16,384 = 491,520$ bytes ($0\text{x}78000$) per surface.
- **VRAM Allocation:**
  - Surface A: `0x10CE6200`
  - Surface B: `0x10D5E200`
  - Gap / Stride: Exactly $491,520$ bytes apart.

```
       0                    320                   640               1024
       +---------------------+---------------------+------------------+
  0    |                     |                     |                  |
       |  Active Playfield   |    Texture Atlas    |  Unused Tile     |
       |     (320 x 240)     |     (320 x 240)     |  Stride Padding  |
       |                     |                     |                  |
 240   +---------------------+---------------------+------------------+
```

During presentation, `SDL_RenderPresent()` instructs the Zevio video display controller to scan out from coordinate $(0, 0)$ of the completed backbuffer, while sprites and textures are drawn from coordinate $(320 + u, v)$ into the playfield.

---

## 4. Freestanding Runtime & Memory Model

Hosted C runtime functions are replaced by freestanding implementations in `src/graphics/sdl2_vflash/SDL_vflashruntime.c`:

1. **Bounded Heap Allocator:**
   A first-fit bounded memory allocator with block coalesce logic manages SDRAM between linker symbols `__heap_start` and `__heap_end`. Exposes introspection statistics:
   - `vflash_sdl2_get_heap_used()`: Current active heap allocations in bytes.
   - `vflash_sdl2_get_alloc_count()`: Total number of active live blocks.
2. **Built-in Memory Operations:**
   Freestanding `memcpy`, `memset`, `memmove`, and `memcmp`.
3. **Hardware Timer:**
   `SDL_GetTicks()`, `SDL_GetTicks64()`, and `SDL_GetPerformanceCounter()` utilize the 10 kHz hardware timer (`0x90010008` / `vflash_clock_ticks()`).
4. **Single-Core ARM Atomics:**
   Atomic primitives (`__atomic_exchange_4`, `__sync_lock_test_and_set_4`) implemented for single-core ARMv5TEJ.
5. **Virtual Embedded Asset Loader:**
   `SDL_RWFromFile` is intercepted for embedded assets (e.g. `icon.bmp`), serving byte buffers directly from read-only memory while forwarding unknown file paths to upstream fallback handlers.

---

## 5. Supported SDL2 API Operations

The following standard SDL2 operations are verified and operational:

| Category | Function | Implementation / Hardware Mapping |
| :--- | :--- | :--- |
| **Lifecycle** | `SDL_Init` | Initializes video subsystem and hardware clock |
| | `SDL_Quit` | Flushes GPU, unbinds renderer, restores linear video scanout |
| | `SDL_CreateWindowAndRenderer` | Creates 320×240 fullscreen window and `"zevio"` renderer |
| | `SDL_DestroyRenderer` | Frees render structures and returns resources to heap |
| | `SDL_DestroyWindow` | Destroys window handle |
| **Draw & Clear** | `SDL_SetRenderDrawColor` | Sets active drawing RGBA color |
| | `SDL_RenderClear` | Queues full playfield fill ($320\times 240$) |
| | `SDL_RenderFillRect` | Queues D3 hardware solid color rectangle fill |
| | `SDL_RenderCopy` | Queues D2/C8 sprite blit from atlas to playfield |
| | `SDL_RenderPresent` | Flips front/back buffer, updates hardware scanout address |
| **Textures** | `SDL_CreateTexture` | Allocates texture descriptor and atlas shelf coordinates |
| | `SDL_CreateTextureFromSurface`| Creates texture from loaded BMP surface |
| | `SDL_DestroyTexture` | Invalidates texture handle |
| | `SDL_UpdateTexture` | Converts host pixels and uploads to both A/B atlases |
| | `SDL_SetTextureBlendMode` | Configures `NONE` (opaque D2) or `BLEND` (translucent C8) |
| **Surfaces** | `SDL_LoadBMP` | Parses standard Windows BMP headers and pixel data |
| | `SDL_SetColorKey` | Configures source transparent color key |
| | `SDL_FreeSurface` | Reclaims temporary surface memory |
| **Events & Timing**| `SDL_PollEvent` | Processes quit and window events |
| | `SDL_GetTicks` | Returns elapsed milliseconds from 10 kHz hardware clock |
| | `SDL_Delay` | Millisecond busy-wait using hardware timer |

---

## 6. Upstream Test Suite Validation

The native port was validated using two upstream SDL2 test programs ported to freestanding V.Flash execution:

1. **`examples/gpu/sdl2_testsprite.c` (Upstream `testspriteminimal`):**
   - 8 bouncing sprites animating across a 320×240 playfield.
   - Clears background to gray `(0xA0, 0xA0, 0xA0)` $\rightarrow$ `(165, 165, 165)`.
   - Loads 16×16 `icon.bmp` via `SDL_CreateTextureFromSurface` with transparent color key.
   - Bounces sprites with wall collision detection and velocity reflection.
   - Verified across 32 frames of real SDL2 execution.
   - Frame output in `frame.ppm` verified: exactly 73,856 gray background pixels, 2,000 yellow smiley pixels, and 944 black outline pixels.
2. **`examples/gpu/sdl2_testdraw.c` (Upstream `testdraw2`):**
   - Renders 5 distinct solid colored test rectangles (Red, Green, Blue, Yellow, White) over a dark blue background.
   - Validates `SDL_SetRenderDrawColor`, `SDL_RenderClear`, and `SDL_RenderFillRect`.

### Acceptance Test Matrix (`tools/gpu_smoke.py --test sdl2`)

All 6 execution variants pass all checks:

| Variant | MMU Mode | View / Final Action | Frames | GPU Lists | Heap Used | Status |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `off-a` | Disabled | Surface A presented | 33 | 299 | 6,752 B | **PASS** |
| `off-b` | Disabled | Surface B presented | 32 | 290 | 6,752 B | **PASS** |
| `off-linear` | Disabled | Clean linear restore | 32 | 290 | 6,752 B | **PASS** |
| `on-a` | Enabled | Surface A presented | 33 | 299 | 6,752 B | **PASS** |
| `on-b` | Enabled | Surface B presented | 32 | 290 | 6,752 B | **PASS** |
| `on-linear` | Enabled | Clean linear restore | 32 | 290 | 6,752 B | **PASS** |

---

## 7. Interactive Launcher

To run the native SDL2 sprite demo in FlashEm:
```bash
./play-homebrew.sh --demo gpu-sdl2 --scale 3
```
Options:
- `--gpu-mmu off|on`: Select MMU translation mode (default: `off`).
- `--gpu-view a|b|linear`: Select final scanout target (default: `a`).
