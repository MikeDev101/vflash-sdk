# Retail Loading Screen Reverse-Engineering & Native Module

This document details the reverse engineering of the VTech V.Flash retail V.DISC loading screen and its clean-room native implementation in the V.Flash SDK.

## 1. Reverse-Engineering Findings

### Firmware Origin vs. Game Origin
Analysis of retail discs and dumped firmware revealed that the retail loading screen originates **entirely within the console BIOS firmware** (`dumps/A29L160ATV@TSOP48.BIN`), specifically around offset `0x0016A4C0` where the font and `"Loading ......"` string reside.

During console power-on:
1. The BIOS initializes the CPU, PMU, and ZEVIO video engine (VE).
2. It displays a brief centered VTech logo and copyright splash.
3. It transitions to the horizontal loading band and begins CD-ROM DMA transfers (DMA channel 7) reading `0SYSTEM/BOOT.BIN` into SDRAM at `0x10000000` / `0x10C00000`.
4. As sectors transfer over DMA, the progress bar updates proportionally to sectors read.
5. Once `BOOT.BIN` transfer finishes, execution jumps to the game's entry point (`_start`).

### Visual Geometry & Palette
Reference capture from FlashEm and retail frame analysis yielded the exact pixel layout:

| Element | Geometry | Color / Palette | Description |
|---|---|---|---|
| Background | Full screen 320×240 | `#00184A` (RGB 0, 24, 74) | Deep midnight navy |
| Central Band | `X=0..319`, `Y=94..138` (45px tall) | `#001842` (RGB 0, 24, 66) | Subtly darker central loading strip |
| Divider Line | `X=0..319`, `Y=139` (1px tall) | `#001031` (RGB 0, 16, 49) | Crisp horizontal shadow line |
| V.DISC Icon | `X=68..97`, `Y=98..136` (30×39px) | Multi-tone (`#3A4B62`, `#7B8EA8`, `#DCE4F0`) | Cartridge caddy, optical disc, "V.DISC" text, 3-lobed scallops |
| "Loading . ." | `X=108`, `Y=104` (7px tall) | `#DCE4F0` | Proportional bitmap text with 4-phase dot animation |
| Status Text | `X=108`, `Y=114` (7px tall) | `#8FA4C4` | Real-time diagnostic status message |
| Progress Capsule | `X=106..250`, `Y=127..134` (145×8px) | Outline `#5A6B8C`, Trough `#1D1D22` | Rounded pill capsule with beveled gradient fill |

## 2. Clean-Room Native Module Implementation

To avoid committing extracted copyrighted retail assets to Git, the SDK implements a 100% clean-room procedural recreation in `src/platform/loading.c` and `include/vflash/loading.h`.

### Architecture & Features
- **Zero Binary Blobs**: All coordinates, outlines, gradients, and glyphs are rendered procedurally using native SDK drawing primitives (`draw_rect`, `draw_pixel`, `draw_text`).
- **SDK Splash Preamble**: Displays the official SDK banner centered beforehand before transitioning into the loading band.
- **Real Diagnostic Progress**: Rather than an arbitrary dummy timer, the progress bar and status text reflect actual initialization milestones.
- **Flicker-Free Double Buffering**: Renders to the off-screen backbuffer and synchronizes flips to vertical blank.
- **MMU ON & OFF**: Fully compatible with both raw physical memory and identity-mapped virtual memory.

### C API Reference

```c
#include "vflash/loading.h"

/* Splash configuration struct (all fields optional, NULL falls back to defaults) */
typedef struct {
    const char *title;     /* Primary title (default: "V.FLASH SDK") */
    const char *subtitle;  /* App / Demo subtitle (default: "Homebrew Application") */
    const char *caption;   /* Descriptive caption (default: "Open-Source Platform") */
    const char *copyright; /* Copyright notice (default: "(C) 2026 SDK Contributors") */
} vflash_loading_config_t;

/* Initialize display with custom splash configuration and double buffering */
void vflash_loading_init_ex(const vflash_loading_config_t *config);

/* Initialize display with generic SDK defaults (not hardcoded to any demo) */
void vflash_loading_init(void);

/* Dynamically update splash title and subtitle text */
void vflash_loading_set_title(const char *title, const char *subtitle);

/* Update loading progress percentage (0 - 100) */
void vflash_loading_set_progress(unsigned percent);

/* Set status diagnostic string (e.g. "Mounting V.DISC", "Validating MJP") */
void vflash_loading_set_status(const char *status);

/* Render frame to backbuffer and present with vertical blank sync */
void vflash_loading_render(void);

/* Finish loading and prepare display buffers for application handoff */
void vflash_loading_finish(void);

/* Query current progress percentage */
unsigned vflash_loading_get_progress(void);

/* Query current status message */
const char *vflash_loading_get_status(void);
```
