# Build the SDK and disc images

Run these commands from the repository root. No firmware or commercial disc is
required to compile, package, or run host tests.

Requirements: CMake 3.20+, Python 3.9+, GNU Arm Embedded GCC/binutils, and Ninja
or Make. ISO authoring additionally requires `xorriso`, `genisoimage`, or
`mkisofs`. Tested on Linux with GCC 14.2.1 and binutils 2.44; no libc/newlib or
C++ library is needed. Host drawing tests and visual verification also use
native `cc` (or the compiler executable specified by `VFLASH_HOST_CC`).

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-vflash.cmake
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target disc_image
```

Use `-DVFLASH_CROSS_PREFIX=/path/to/bin/arm-none-eabi-` for a compiler outside
PATH. Missing tools stop configuration. Omit `-G Ninja` to use Make.

Outputs:

* `build/vflash_probe.elf`: independent direct-load experiment at `0x10C00000`.
* `build/vflash_disc_probe.elf`: disc payload linked at `0x10C00018`.
* `.bin`, `.map`, `.dis`, `.readelf.txt` companions for each ELF.
* `build/BOOT.BIN`: deterministic, sector-padded executable, generated from
  the disc ELF without a commercial template.
* `build/homebrew.iso`: ISO 9660 filesystem containing `0SYSTEM/BOOT.BIN;1`.
* `build/homebrew.bin` and `build/homebrew.cue`: single Mode 1/2352 track with
  generated EDC/ECC. **Open the CUE for the firmware boot test.**

The demo sets its stack, clears `.bss`, preserves the boot checks, draws every
pixel in software, configures a linear 320×240 xBGR1555 scanout layer, and loops.
There is no graphics acceleration, audio, heap, libc, threading, or filesystem
dependency. Flags select ARM926EJ-S, ARM state, little endian,
AAPCS/EABI5, and software floating point. The two linker layouts and their
limits are explained in [hardware.md](../reference/hardware.md).

## Inspect, package, and master manually

```sh
python3 tools/inspect_boot.py /path/to/BOOT.BIN
python3 tools/verify_elf.py build/vflash_disc_probe.elf
python3 tools/pack_boot.py build/vflash_disc_probe.elf /tmp/probe-BOOT.BIN --verbose
sh tools/make_iso.sh /tmp/probe-BOOT.BIN /tmp/probe.iso
python3 tools/raw_cd.py /tmp/probe.iso /tmp/probe.cue
```

These commands refuse existing output files. The inspector is read only,
bounds checked, capped at 64 MiB, and explicitly treats `REL\0` / `DBG\0`
signature hits as observations rather than parsed chunks. The packer supports
only the tested fixed-address takeover model; it does not implement a µMORE
application ABI, relocation, or compression. BOOT output is deterministic;
ISO/BIN hashes can vary with filesystem dates and generator metadata.

See [boot format](../reference/boot-format.md) and [emulator tests](testing.md).

## Build a separate panic diagnostic

```sh
./scripts/panic/build-undefined.sh
./scripts/panic/build-manual.sh --parallel 4
```

The ten executable wrappers live under `scripts/panic/`. They work from any
working directory, configure `build/` when needed, and print the ELF and CUE
paths. Arguments are forwarded to `cmake --build`. The diagnostic list and
exception limitations are in [the panic reference](../reference/panic.md).

CMake target definitions are in `cmake/VFlashTargets.cmake`; disc-image rules
are shared by all examples through `cmake/DiscImage.cmake`. Each executable
compiles its own SDK sources because video buffering and nested-fault injection
are intentionally different compile-time configurations.
