# Documentation

Commands in these documents run from the repository root unless stated otherwise.
The supported evaluation target is the pinned FlashEm interpreter. Physical
hardware, a firmware-managed application ABI, SDL2 and ScratchEverywhere ports
remain outside the implemented baseline.

## Guides

- [Build the SDK and disc images](guides/building.md): tools, targets, outputs,
  per-test scripts and manual BOOT/CD packaging.
- [Run FlashEm, firmware and games](guides/running.md): homebrew, BIOS menu,
  controller keys, local dumps and virtual memory cards.
- [Verify and debug](guides/testing.md): pinned emulator build, host tests,
  firmware/direct boot checks, screenshots and debugger commands.

## Implementation references

- [CPU, memory and takeover contract](reference/hardware.md)
- [BOOT.BIN format and raw disc packaging](reference/boot-format.md)
- [CPU framebuffer and video engine](reference/video.md)
- [Animation, device timing and double buffering](reference/animation.md)
- [Panic records, vectors and exception limitations](reference/panic.md)
- [Printable ASCII font and atlas](reference/fonts.md)

- [Optional MMU bring-up and emulator audit](reference/mmu.md)

- [GPU HAL and native-fill investigation](reference/gpu.md)
- [Restricted native GPU HAL integration](reference/gpu-native-hal.md)
- [Direct tiled scanout, D2 and D1 experiments](reference/gpu-tiled.md)
- [Zevio graphics register inventory](reference/zevio-registers.md)

## Research and history

- [Firmware identity and loader evidence](research/firmware.md)
- [Pinned sources and future integration audit](research/sources.md)
- [Initial RAM-marker boot](history/initial-boot.md)
- [Initial graphical boot](history/graphical-boot.md)
- [Initial exception milestone](history/panic-milestone.md)

Research is scoped to its identified input/revision. History preserves old
addresses, sizes, hashes and test counts; it is not a current memory map or
roadmap. Current runs resolve symbols from their ELF and save evidence in the
chosen ignored build directory.
