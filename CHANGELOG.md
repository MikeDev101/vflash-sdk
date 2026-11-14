# Changelog

All notable changes to the V.Flash SDK are documented in this file.

## [1.0.0-rc.2] - 2026-11-13

> **Validation Status:** **FlashEm-validated, physical hardware unverified.**  
> All RC2 checks were executed in FlashEm using a local V.Flash BIOS dump. Behavior on physical V.Flash / V.Smile Pro consoles has not yet been tested.

### Added
- **MMU & Write-Back D-Cache Coherency (`009509f`)**: Enabled ARM926EJ-S MMU, instruction cache, and write-back data cache with GPU MMIO mapping (`0xA8000000`) and explicit DMA cache maintenance across GPU, audio, video, and CD-ROM drivers.
- **GPU Render-to-Texture & `SDL_RenderGeometry()` (`009509f`, `fcf9d6b`)**: Added persistent offscreen render targets (`SDL_TEXTUREACCESS_TARGET`) with atlas-tile overflow synchronization and textured/untextured `SDL_RenderGeometry()` support.
- **Hardware-Accelerated Sprite Transforms (`ed3a706`)**: Added Zevio 3D GPU textured quad/triangle rendering for arbitrary scaling, rotation, flipping, alpha blending, and edge clipping via `SDL_RenderCopyEx()` / `SDL_RenderCopyExF()` and native GPU APIs.
- **2D Free-List Atlas Allocator & Diagnostics (`7803ae0`, `e40eeef`, `912db7e`)**: Replaced bump allocation with a coalescing 2D free-list atlas allocator, separated OOM vs. fragmentation vs. metadata exhaustion diagnostic counters, and added mixed-size fragmentation/pinwheel tests.

### Fixed
- **Atlas Metadata Capacity Invariant (`c67804b`)**: Replaced `atlas_free_overflow` with `atlas_live_count` and enforced `atlas_free_count + atlas_live_count <= 128` (`VFLASH_MAX_ATLAS_FREE_RECTS`) in `alloc_atlas_slot()`, guaranteeing texture destruction cannot overflow free-list metadata or silently drop freed space.
- **ARMv5TEJ `__sync_synchronize()` (`c67804b`)**: Removed `-specs=sync-none.specs` and implemented `__sync_synchronize()` using ARMv5TEJ CP15 `mcr p15, 0, r0, c7, c10, 4` (Drain Write Buffer) plus a compiler memory barrier (`"memory"` clobber).
  - *Note*: Draining the write buffer orders buffered stores and memory-mapped writes relative to the compiler barrier, but does not replace D-cache clean/invalidate maintenance for DMA or IRQ critical sections for interrupt-handler synchronization.

### Verification
- Clean rebuild (`cmake --build build --clean-first`): passed
- CTest (`host_tools`, `elf_contract`, `disc_elf_contract`, `animation_elf_contract`): 4/4 passed
- Host unit tests (`python3 -m unittest discover -s tests -v`): 63/63 passed
- `emulator_smoke.py --suite suite` (MMU off & on): 2/2 passed
- `gpu_smoke.py` (`renderer`, `lifecycle`, `alpha`, `sdl2`; MMU off & on × views `a`, `b`, `linear`): 24/24 passed
- `gpu_transform_demo` (`emulator_smoke.py`, 900 frames, MMU off & on): passed (`transform_status = 0x600d600d`, `transform_errors = 0x0`)
- `arm-none-eabi-objdump` link-stage verification: confirmed `__sync_synchronize` emits `mcr 15, 0, r3, cr7, cr10, {4}` and `SDL_AtomicGet` / `SDL_AtomicUnlock` branch to it

## [1.0.0-rc.1] - 2026-11-13

- Initial release candidate (`a12eebd`).
