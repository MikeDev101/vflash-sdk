# Animated CPU rendering in FlashEm

The animation is a separate firmware-booted disc. Hello World remains the
default static demo and retains its full-frame reference test.

```sh
./play-homebrew.sh --demo animation
```

Use `--demo hello` to explicitly select the original display. The normal
`--scale`, `--bios`, `--emulator`, and optional `--memcard` arguments work in
both modes. Animation images are generated under `build/animation/`; runtime
settings and F5 screenshots are under `build/play-animation/`. Game and BIOS
launchers do not accept `--demo`.

## Rendering and double buffering

The ARM CPU redraws the entire 320×240 xBGR1555 scene each frame: background,
timing text, Hello World, arena, bouncing 24×24 square and RGB strip. It does
not submit graphics-engine lists or reuse firmware drawing services. Two
separate, 64-byte-aligned `.bss` arrays each hold 153,600 bytes. Together they
use 307,200 bytes of pixel storage, excluding code/other state and stack.
The existing linker assertion and ELF checks keep both arrays below the
reserved 16 KiB stack. Their addresses are resolved from the ELF for testing.

`vflash_video_backbuffer` returns the array that is not currently scanned out.
Presentation rejects the active front buffer. After drawing, a compiler memory
barrier orders the stores, and the code clears video-engine status bit 1 at
`0xB8000000`, waits for a fresh event, then writes the completed buffer address
to `0xB8000160`. Color layer 0 remains the only enabled layer. The next modeled
`ve_render` scans the new bank. The CPU subsequently draws only into the other
bank. MMU/caches remain off as established by the firmware handoff.

This synchronization follows FlashEm commit
`fb109dd01a49239c5884fcec60b9dc451fb651f8`, [src/hw.c](https://github.com/WizzardSK/flashem-libretro/blob/fb109dd01a49239c5884fcec60b9dc451fb651f8/src/hw.c):
`ve_write` implements write-one-to-clear status; `ve_vblank` calls `ve_render`
and sets bit 1 once per emulated video frame. Because rendering occurs before
the bit is set, our address change takes effect at the next modeled scanout.
No interrupt handler is installed. A 100 ms device-time timeout detects a
missing vblank and leaves a failure status instead of waiting indefinitely.

## Device clock and on-screen counters

`clock.c` owns timer-pair 0, timer 0 at `0x90010000`. The model's
`pmu_set_clocks` and `timer_advance` give a 37.5 MHz timer clock at the inspected
ROM's active PMU setting `0x00000100`. The application verifies that setting;
it does not change CPU clocks. It disables this pair's IRQ mask through
`0x900A0034`, stops the timer, sets divider 3749, resets its value and selects
count-up/free-running control `0x0F`. The modeled divider is inclusive, so the
result is a 10 kHz timer, or **100 microseconds per tick**.

The timer is 16 bits and wraps every 6.5536 seconds. Unsigned 16-bit differences
handle wrap across the short intervals sampled here. FPS counts completed
presentations over at least 10,000 device ticks and rounds to an integer.
`CPU: ...ms` shows the preceding frame's measured redraw duration, excluding
the vblank wait. Timing text itself is part of the redraw workload. The first
FPS window starts at application entry; the display settles after that first
window. Motion advances by a fixed step per presented frame.

The animation links only GCC's static compiler arithmetic helpers for integer
division. It still uses no libc, heap, floating point, threads or OS services.
The original static targets retain their existing library-free configuration.

These are measurements against **FlashEm's device clock and instruction/timing
model**, rather than host wall-clock FPS or a physical-console benchmark.
The scene establishes a repeatable baseline for software rendering and frame
pacing.

## Verification and observed result

```sh
cmake --build build --target all disc_image animation_disc_image
ctest --test-dir build --output-on-failure
python3 tools/emulator_smoke.py \
  --emulator build/emulator/flashem \
  --bios 'dumps/A29L160ATV@TSOP48.BIN' \
  --elf build/vflash_animation_probe.elf \
  --disc build/animation/homebrew.cue \
  --out build/animation-smoke --verify-animation
```

The original NOR firmware boots the animation without `--load`. Acceptance
requires the original data/status markers, video/render success, advancing
frame state, valid bounce coordinates, alternating scanout addresses covering
both owned arrays, and visible square movement in two actual captures. It
also compares every native front-buffer pixel and every presented RGB pixel
with the same C animation renderer compiled for the host, using the frame
metadata recorded by the ARM program. No expected image is injected into
the emulator. Unmodeled MMIO from application code remains a failure.

The initial verified sample reported **60 presented FPS**, **13.4 ms** for a
full redraw, and a **16.7 ms** frame interval. Both bank addresses alternated
through hundreds of flips and the captured square position matched the ARM
metadata and host renderer. This is one sample under the pinned emulator,
not a maximum-workload or long-duration benchmark. Physical hardware remains
outside the current evaluation scope.

`build/animation-smoke/result.json` records exact per-run counters and hashes;
`frame.ppm` is the final actual output; `frame-*.ppm` show movement; and
`frame.bgr555` contains the front buffer from the RAM dump. Host tests cover
10,000 movement updates/bounces, output changes, buffer canaries, and the
separate launcher/build target. Existing static rendering and tooling checks
remain part of the suite.
