# 2D Renderer Abstraction (`vflash_renderer_t`)

The 2D renderer abstraction (`include/vflash/renderer.h`, `src/graphics/renderer.c`)
provides a lightweight, mid-level drawing and asset-management layer above the
low-level `vflash_gpu_*` hardware abstraction layer.

It encapsulates:
- Hardware double-buffering ($A \leftrightarrow B$ surface ping-pong with vblank synchronization).
- Automatic offscreen texture atlas placement and packing (shelf allocator).
- Linear SDRAM asset uploading (`vflash_renderer_upload_texture`).
- Scissor clipping (`vflash_renderer_set_clip` / `vflash_renderer_reset_clip`).
- Playfield clearing (`vflash_renderer_clear`) and solid rectangle fills (`vflash_renderer_fill_rect`).
- Multi-mode sprite rendering (`OPAQUE`, `KEYED`, `TRANSLUCENT`).
- Clean shutdown with linear scanout recovery.

Like the low-level HAL, this abstraction is freestanding, performs no dynamic heap
allocations, and requires caller-owned static or identity-mapped SDRAM buffers.

---

## 1. Architecture and Memory Layout

The renderer operates on two caller-provided tiled surfaces (`surface_a` and `surface_b`).
Each surface is partitioned horizontally into:
1. **Visible Playfield:** $x \in [0, \text{playfield\_width})$, presented to the display controller layer 0.
2. **Offscreen Texture Atlas:** $x \in [\text{atlas\_x}, \text{atlas\_x} + \text{atlas\_width})$, used to store uploaded sprite assets.

```
+------------------------------------+-------------------------+
|                                    |   Offscreen Atlas       |
|    Visible Playfield Viewport      |   (x=96..127, y=0..31)  |
|    (e.g., 96 x 32)                 |   - BGR555 asset        |
|                                    |   - ABGR4444 asset      |
+------------------------------------+-------------------------+
|<---------- 96 pixels ------------->|<------ 32 pixels ------>|
|<------------------------ 128 pixels ------------------------>|
```

Because FlashEm models a single active tiled backing surface (`0xA8000098`) for
same-surface D2 copies and C8 texture fetches, keeping the atlas in offscreen tiles
of the same backing buffer enables fast local copies and texture lookups without
cross-surface blitting.

### Allocation and Alignment Requirements
- `surface_width`: Multiple of 32 pixels ($32 \le w \le 1024$).
- `surface_height`: Multiple of 8 pixels ($8 \le h \le 1024$).
- `surface_bytes`: At least $(h / 8) \times 16384$ bytes.
- `surface_a` and `surface_b`: 64-byte aligned identity physical addresses in SDRAM (`0x10000000..0x10FFFFFF`).
- `playfield_width`: Multiple of 32 pixels, $\le \text{surface\_width}$.
- `playfield_height`: Multiple of 8 pixels, $\le \text{surface\_height}$.
- `screen_x`, `screen_y`: Placement coordinates on the 320x240 display ($x + w \le 320$, $y + h \le 240$).

---

## 2. Texture Modes (`vflash_texture_type_t`)

The renderer supports three distinct texture types:

| Type | Underlying GPU Command | Pixel Format | Behavior |
| --- | --- | --- | --- |
| `VFLASH_TEXTURE_OPAQUE` | `0xD2` region copy | `BGR555` | Opaque block copy; preserves all 16 bits; clips against active clip window |
| `VFLASH_TEXTURE_KEYED` | `0xC8` textured sprite | `BGR555` | Transparent color-key: `0x0000` and bit 15 (`0x8000`) are transparent |
| `VFLASH_TEXTURE_TRANSLUCENT` | `0xC8` textured sprite | `ABGR4444` | Per-pixel 4-bit alpha blending ($0 = \text{transparent}, 15 = \text{opaque}$) |

Textures are uploaded once via `vflash_renderer_upload_texture` into both $A$ and $B$ surfaces
at identical atlas offsets. Sub-rectangles of textures can be rendered using
`vflash_renderer_draw_sub`.

---

## 3. Clipping and Scissor Semantics

- **Default Clip:** Upon initialization, the clip rectangle is set to the visible playfield $(0, 0, \text{playfield\_width}, \text{playfield\_height})$.
- **Custom Clip:** `vflash_renderer_set_clip(r, x, y, w, h)` clamps the specified rectangle to the playfield bounds.
- **Reset:** `vflash_renderer_reset_clip(r)` restores the clip to the full playfield.
- **Handling Opaque D2 Copies:** The low-level Zevio D2 region-copy command does not have hardware scissor registers. `vflash_renderer_draw` and `vflash_renderer_draw_sub` calculate the intersection with the active clip window, adjust the source UV and destination coordinates in software, temporarily elevate the surface clip for the D2 hardware check, and restore the active clip immediately after emission.

---

## 4. API Reference

### Initialization & Lifecycle
```c
vflash_gpu_result_t vflash_renderer_init(vflash_renderer_t *r, const vflash_renderer_config_t *config);
vflash_gpu_result_t vflash_renderer_shutdown(vflash_renderer_t *r);
vflash_gpu_result_t vflash_renderer_reset_atlas(vflash_renderer_t *r);
vflash_gpu_result_t vflash_renderer_destroy_texture(vflash_renderer_t *r, vflash_texture_t *tex);
```
- Initializes the video controller, clears the linear fallback buffer, registers tiled surfaces, and binds the Zevio GPU backend.
- `reset_atlas` waits for GPU idle, safely parks the display controller back to the linear fallback buffer (clearing any active frontbuffer lock), resets the shelf allocator to the start of the atlas, increments the internal `generation_id` (invalidating all existing texture handles in $O(1)$), and resets the backbuffer pointer.
- `destroy_texture` verifies the texture handle matches the active generation, zeroes its `id`, `width`, and `height`, and renders the handle permanently invalid.
- `shutdown` waits for GPU idle, unbinds the GPU, and restores linear scanout.

### Texture Upload & Frontbuffer Protection
```c
vflash_gpu_result_t vflash_renderer_upload_texture(vflash_renderer_t *r, vflash_texture_t *tex,
                                                   const void *pixels, uint32_t width, uint32_t height,
                                                   vflash_texture_type_t type);
```
- Allocates an offscreen sub-region from the atlas using a row shelf allocator (4-pixel word aligned).
- If the requested texture exceeds the remaining atlas shelf space, returns `VFLASH_GPU_OUT_OF_MEMORY` without altering allocator pointers.
- Preserves frontbuffer write protection: if either surface $A$ or surface $B$ is actively being scanned out by the display controller (`vflash_video_tiled_address() != 0`), direct uploads are rejected with `VFLASH_GPU_BUSY` to prevent screen tearing or memory conflicts. Rebuilding an atlas should be performed between scenes with `vflash_renderer_reset_atlas()`.
- Uploads pixel data to both $A$ and $B$ backing allocations via D0/0E streaming with texture-cache flush (`0x89`).
- Populates `tex` with the assigned `atlas_x`, `atlas_y`, `width`, `height`, `type`, and the renderer's current `generation_id`.

### Drawing Operations
```c
vflash_gpu_result_t vflash_renderer_clear(vflash_renderer_t *r, uint32_t rgb);
vflash_gpu_result_t vflash_renderer_fill_rect(vflash_renderer_t *r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);
vflash_gpu_result_t vflash_renderer_draw(vflash_renderer_t *r, const vflash_texture_t *tex, int32_t dx, int32_t dy);
vflash_gpu_result_t vflash_renderer_draw_sub(vflash_renderer_t *r, const vflash_texture_t *tex,
                                             int32_t sx, int32_t sy, int32_t w, int32_t h,
                                             int32_t dx, int32_t dy);
```
- Draw operations reject destroyed textures (`tex->id == 0`) and stale handles from previous atlas generations (`tex->id != r->generation_id`) with `VFLASH_GPU_INVALID`.

### Frame Presentation
```c
vflash_gpu_result_t vflash_renderer_present(vflash_renderer_t *r);
```
- Waits for pending GPU lists to complete.
- Presents the active backbuffer's playfield view at `(screen_x, screen_y)` synchronized with vblank.
- Flips the active backbuffer pointer ($A \leftrightarrow B$) and reapplies the active clipping bounds.

---

## 5. Verification Matrix

The renderer abstraction and its lifecycle management are validated across two dedicated smoke suites:
1. **`tools/gpu_renderer_smoke.py`:** Validates basic double-buffering, multi-mode sprite rendering (opaque, keyed, translucent), scissor clipping, and linear scanout recovery across 6 variants.
2. **`tools/gpu_lifecycle_smoke.py`:** Validates multi-scene lifecycle transitions:
   - Scene 1 asset uploads and 16 frames of animation.
   - Atlas exhaustion rejection (`VFLASH_GPU_OUT_OF_MEMORY`) without memory pointer corruption.
   - Frontbuffer upload protection rejection (`VFLASH_GPU_BUSY`) during active display scanout.
   - Individual texture destruction (`destroy_texture`) and stale handle rejection (`VFLASH_GPU_INVALID`).
   - Atlas reset (`reset_atlas`), linear parking, generation advancement, and Scene 2 re-allocation with distinct texture geometry.
   - Scene 2 rendering for 16 frames (total 32 frames) with exact bit-level dual-surface coherency ($A$ and $B$ surfaces identical) and scanout verification.

