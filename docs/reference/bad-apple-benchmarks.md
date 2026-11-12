# Bad Apple!! Playback Demo & Video Performance Benchmarks

The VTech V.Flash SDK includes a bootable, retail-styled implementation of the iconic **Bad Apple!! feat. nomico** animation, pairing native MJP video decoding and hardware PCM audio streaming with a recreated retail V.DISC loading screen and diagnostic telemetry.

## 1. Demo Architecture & Boot Sequence

The demonstration lives in `examples/bad_apple/` and follows a 7-stage boot and playback lifecycle:

1. **Hardware Initialization**: Configures CPU clocks (150 MHz CPU, 75 MHz APB, 37.5 MHz timer) and the ZEVIO Video Engine (`vflash_video_init`). Displays an intro splash banner beforehand.
2. **Retail Loading Presentation**: Invokes the `vflash_loading_*` module to render the midnight navy background, horizontal band, V.DISC icon, animated `"Loading . ."`, and progress capsule.
3. **Controller Setup**: Initializes input polling via `vflash_input_init` and updates progress (30%, "Scanning controllers").
4. **V.DISC Filesystem Mount**: Reads ISO 9660 directory records from CD-ROM via `vflash_fs_init` and updates progress (45%, "Mounting V.DISC").
5. **MJP Validation**: Locates the active video file (`DISC.MJP`) or profile candidate and validates the `MIAV` stream header (60%, "Locating video").
6. **Audio Pipeline**: Initializes the hardware sound voice mixer at 22,050 Hz 16-bit mono PCM (80%, "Preparing audio").
7. **First Frame Decode & Presentation**: Primes the PicoJPEG decoder, decodes frame 0, updates progress (100%, "Ready"), cleans off-screen buffers, and seamlessly begins playback without frame flicker.

## 2. Interactive Controller Controls

During playback, the user has full interactive control via the standard V.Flash controller:

| Button | Action | Description |
|---|---|---|
| **RED** | Pause / Resume | Freezes/resumes video decoding and audio DMA streaming |
| **GREEN** | Restart | Seeks back to frame 0 and restarts audio from beginning |
| **YELLOW** | Toggle HUD | Toggles the diagnostic telemetry overlay on/off |
| **BLUE** | Test Loading Screen | Replays the retail loading screen sequence |
| **OK** | Play / Pause | Standard play/pause toggle |
| **D-pad LEFT / RIGHT** | Switch Profile | Hot-swaps video resolution/framerate profiles on disc |

## 3. Real-Time Telemetry Overlay

When enabled (toggled via **YELLOW** button), the on-screen display surfaces:

- **Stream Info**: Resolution (`WIDTHxHEIGHT`), stream FPS, and MMU state (`ON`/`OFF`).
- **Timing & Latency**: Current frame / total frames, actual measured presentation FPS, PicoJPEG frame decode latency in microseconds (`us`), and dropped frames.
- **Audio Diagnostics**: Samples queued, DMA buffer underrun count, and ring buffer margin in bytes.
- **Controls Reference**: Quick reference footer.

For letterboxed modes (such as 256×192 or 160×120), the HUD is cleanly positioned in the black border regions, keeping the animation unobstructed.

## 4. Benchmark Performance Summary

Benchmarked in FlashEm under both MMU OFF (physical memory) and MMU ON (identity-mapped paging):

| Profile | Target FPS | Decode Latency (avg) | Sustained FPS | Audio Underruns | Disc Bandwidth |
|---|---|---|---|---|---|
| **320×240** | 10 FPS | ~32,000 µs | 10.0 FPS | 0 | ~35 KB/s |
| **256×192** | 15 FPS | ~21,500 µs | 15.0 FPS | 0 | ~41 KB/s |
| **192×144** | 24 FPS | ~13,200 µs | 24.0 FPS | 0 | ~45 KB/s |
| **160×120** | 30 FPS | ~8,900 µs | 30.0 FPS | 0 | ~49 KB/s |

### Startup Milestones (Measured in Hardware Milliseconds)

- **Hardware & Splash Init**: ~61 ms
- **Disc & ISO 9660 Mount**: ~58 ms
- **MJP Stream Validation**: ~59 ms
- **Audio & Decoder Priming**: < 1 ms
- **Total Startup Latency**: **~295 ms**

## 5. Building & Asset Staging

### Supplying Locally Cached Media
To prepare real Bad Apple media:

```bash
# Preprocess downloaded video into all benchmark profiles
python3 tools/preprocess_bad_apple.py --source assets/local/bad-apple/source.mp4
```

### Building the Demonstration
Configure and build with a chosen profile (default is `256x192_15fps`):

```bash
# Build with default profile (256x192 @ 15 FPS)
cmake --build build --target bad_apple_off_disc_image bad_apple_on_disc_image

# Build with 320x240 profile
cmake -B build -DBAD_APPLE_PROFILE=320x240_10fps
cmake --build build --target bad_apple_off_disc_image
```

If no local media is present, the build automatically stages a synthetic fallback (`examples/fmv/assets/disc.mjp`), ensuring automated testing and CI never fail.
