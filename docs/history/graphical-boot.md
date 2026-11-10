# Initial graphical boot milestone — 2026-11-10

This is a historical snapshot of the first Hello World display, before the
panic handler and complete ASCII font were added. Addresses, sizes and test
counts below belong to that build. See the [video reference](../reference/video.md)
for the current implementation and acceptance commands.

## Original framebuffer allocation

`video_pixels` is a 64-byte-aligned array in the application's `.bss`, assigned
by the existing linker rather than a fixed borrowed firmware address. In that
original disc build it occupies `0x10C00800..0x10C25FFF`; obtain the current
address from `arm-none-eabi-nm` after a rebuild. The executable, constants and
initial data precede it. `__image_end` follows it at `0x10C26000`, well below
the reserved stack at `0x10FFC000..0x10FFFFFF`. Existing linker assertions
reject overlap with that stack, and the ELF/packer validate SDRAM bounds.

## Original validation notes

Validation completed on Linux with GCC Arm 14.2.1, binutils 2.44, Python 3.13.5
and host SDL2 2.32.4: all **24 host unittest methods** and both ELF contract
checks pass (three CTest suites). The firmware-driven `--verify-video` run
passes all five status/data checks, all 153,600 native framebuffer bytes and
all 230,400 presented RGB bytes. The generated BOOT is 2,048 bytes; its larger
framebuffer remains NOLOAD RAM rather than disc content. No emulator source
change, ELF injection, or external drawing overlay is used to obtain the image.
The native graphical frontend was also launched with `play-homebrew.sh`, and
the user confirmed seeing Hello World and the RGB bar. The interactive launcher
uses normal pacing and stays open until closed. Firmware is now stored locally
under `dumps/`; use that path with `--bios` for the automated command above.
