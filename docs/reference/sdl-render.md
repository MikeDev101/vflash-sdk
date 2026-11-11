# SDL2-Compatible Rendering Adapter (`include/vflash/sdl_render.h`)

The SDL2-compatible rendering layer provides a source-compatible, freestanding subset
of the SDL2 2D Render API built on top of `vflash_renderer_t`. It allows developers
familiar with standard SDL2 drawing and texture operations to target the VTech V.Flash
Zevio GPU engine without requiring dynamic memory allocation (`malloc`), windowing,
or platform event subsystems.

---

## 1. Supported SDL2 API Operations

| SDL2 Operation | V.Flash Implementation | Notes |
| :--- | :--- | :--- |
| `SDL_CreateRenderer_Static` | `vflash_renderer_init` | Freestanding: uses caller-provided `SDL_Renderer` storage |
| `SDL_DestroyRenderer` | `vflash_renderer_shutdown` | Waits for GPU idle, unbinds GPU, restores linear scanout |
| `SDL_CreateTexture` | Allocate texture descriptor | Allocates from static bounded pool (`SDL_MAX_TEXTURES = 16`) |
| `SDL_DestroyTexture` | `vflash_renderer_destroy_texture` | Invalidates handle; zeroes width/height in $O(1)$ |
| `SDL_SetTextureBlendMode` | Set blend mode | Configures `NONE` or `BLEND`. Frozen once uploaded |
| `SDL_GetTextureBlendMode` | Query blend mode | Returns active `SDL_BlendMode` |
| `SDL_UpdateTexture` | `vflash_renderer_upload_texture` | Converts CPU pixels and streams to dual-surface atlas |
| `SDL_SetRenderDrawColor` | Set draw color | RGBA color stored in renderer context |
| `SDL_GetRenderDrawColor` | Query draw color | Retrieves current RGBA color |
| `SDL_RenderClear` | `vflash_renderer_clear` | Clears **entire** playfield; ignores active clip |
| `SDL_RenderFillRect` | `vflash_renderer_fill_rect` | Fills solid rectangle clipped to active scissor |
| `SDL_RenderSetClipRect` | `vflash_renderer_set_clip` | Sets scissor; `NULL` or non-positive resets to playfield |
| `SDL_RenderGetClipRect` | Query active clip | Returns active `SDL_Rect` |
| `SDL_RenderIsClipEnabled`| Query clip status | Returns 1 if active, 0 if reset |
| `SDL_RenderCopy` | `vflash_renderer_draw_sub` | Draws unscaled sprite; validates bounds and scaling |
| `SDL_RenderPresent` | `vflash_renderer_present` | Presents backbuffer synchronized with vblank; flips A/B |
| `SDL_RenderResetAtlas` | `vflash_renderer_reset_atlas` | Reclaims atlas storage and clears frontbuffer lock |
| `SDL_GetError` / `SDL_SetError` | Diagnostic strings | Thread-safe single-thread error string buffer |

---

## 2. Pixel Format Conversion & Native Hardware Mapping

SDL2 standard 32-bit format constants (`SDL_PIXELFORMAT_*`) are supported and converted on the CPU during `SDL_UpdateTexture`:

```
Input Format (SDL2)        Blend Mode              Native Hardware Representation
---------------------------------------------------------------------------------
SDL_PIXELFORMAT_ARGB8888   SDL_BLENDMODE_BLEND  -> Native ABGR4444 (C8 Sprite)
SDL_PIXELFORMAT_RGBA8888   SDL_BLENDMODE_BLEND  -> Native ABGR4444 (C8 Sprite)
SDL_PIXELFORMAT_ABGR8888   SDL_BLENDMODE_BLEND  -> Native ABGR4444 (C8 Sprite)
SDL_PIXELFORMAT_ARGB4444   SDL_BLENDMODE_BLEND  -> Native ABGR4444 (C8 Sprite)
SDL_PIXELFORMAT_ABGR4444   SDL_BLENDMODE_BLEND  -> Native ABGR4444 (C8 Sprite)
SDL_PIXELFORMAT_RGB555     SDL_BLENDMODE_NONE   -> Native BGR555   (D2 Region Copy)
SDL_PIXELFORMAT_BGR555     SDL_BLENDMODE_NONE   -> Native BGR555   (D2 Region Copy)
SDL_PIXELFORMAT_ARGB8888   SDL_BLENDMODE_NONE   -> Native BGR555   (D2 Region Copy)
SDL_PIXELFORMAT_RGBA8888   SDL_BLENDMODE_NONE   -> Native BGR555   (D2 Region Copy)
```

### Alpha & Black Handling in `ABGR4444`:
1. **Full Transparency:** Any pixel with $A = 0$ is converted to $A_4 = 0$ ($0\text{x}0\text{xxx}$). The hardware texture engine discards these texels.
2. **Opaque Black:** Unlike color-keyed BGR555 (where $0\text{x}0000$ and bit 15 are transparent), in `ABGR4444` opaque black $(0, 0, 0, 255)$ maps to $0\text{xF}000$. The hardware renders this texel as true opaque black without color keying.
3. **Semi-Transparency:** Pixels with $0 < A < 255$ map to $0 < A_4 < 15$ and are blended per-pixel via the Zevio alpha engine.

---

## 3. Deliberate Constraints & Compatibility Guards

To prevent hidden bugs, memory corruptions, or display tearing, the adapter enforces several intentional constraints:

1. **No Hardware Scaling in `SDL_RenderCopy`:**
   The Zevio engine does not provide hardware bilinear or nearest-neighbor texture stretching.
   `SDL_RenderCopy` verifies that `src_w == dst_w` and `src_h == dst_h`. If dimensions differ, the call fails immediately with `-1` and sets `"unsupported scaling in SDL_RenderCopy"`.
2. **Setup-Phase Texture Uploads:**
   `SDL_UpdateTexture` is supported during asset loading (before the first `SDL_RenderPresent` or after calling `SDL_RenderResetAtlas`). Once active double-buffering is scanning out, live texture uploads are rejected with `-1` (`"live texture update rejected during tiled scanout"`) to respect frontbuffer write protection.
3. **Full Texture Updates Only:**
   Partial texture updates (`rect != NULL` with sub-rectangles) are rejected with `-1` (`"partial texture update unsupported"`). Callers should pass `NULL` or `{0, 0, w, h}`.
4. **Immutable Blend Mode After Upload:**
   A texture's blend mode determines whether it is converted to `BGR555` or `ABGR4444` at upload time. Changing `SDL_SetTextureBlendMode` to an incompatible mode after upload is rejected with `-1` to prevent interpreting BGR555 texels as ABGR4444.
5. **Full-Playfield Clear on `SDL_RenderClear`:**
   In compliance with the SDL2 specification, `SDL_RenderClear` temporarily overrides any active scissor clip, clears the entire 96x32 playfield with the current draw color, and restores the active clip afterwards.

---

## 4. Verification Matrix

The adapter is verified by `tools/gpu_smoke.py --test sdl` across all 6 MMU and scanout configurations:
- MMU: `off` and `on`.
- View: `a` (surface A at 0,0), `b` (surface B at 224,208), and `linear` (clean linear restore).
- Verification checks: 32 frames of animation, 292 GPU command lists per variant, exact bit-level memory on playfield and atlas, zero frontbuffer modifications, and exact pixel output in `frame.ppm`.
