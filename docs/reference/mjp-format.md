# V.Flash Motion JPEG (.MJP) Reverse Engineering Report & Specification

## 1. Executive Summary & Hardware Decoding Investigation

An exhaustive reverse engineering analysis was conducted across:
1. **FlashEm emulator source and binary** (`build/emulator/flashem`).
2. **Retail game disc image** (*Wacky Race on Jumpin' Bean Island* (USA) Rev 2).
3. **Retail game executable** (`BOOT.BIN` / `FA.VFF`).
4. **V.Flash System BIOS** (`dumps/A29L160ATV@TSOP48.BIN`).

### Key Findings on Hardware Acceleration
- **No Hardware Video Decoder Exists**:
  - Examination of FlashEm's MMIO dispatcher (`mmio_rd` / `mmio_wr`) reveals peripherals for ARM926EJ-S, Video Engine (`0xB8000000`), 2D Graphics Engine (`0xA8000000`), Timers (`0x900A0000`), UART (`0x900D0000`), CD controller (`0x16000000`), and Audio/ZSP400 DSP (`0xC0000000`). No JPEG/MPEG or video decoding coprocessor is emulated or present in the hardware map.
  - FlashEm contains no host-side MJP or JPEG decoding hook.
- **ZSP400 DSP Role**:
  - The ZSP400 DSP is exclusively dedicated to sound synthesis (MIDI, sound effect mixing) and is idle during video frame decoding.
- **ARM926EJ-S Software Decoding**:
  - Disassembly of `BOOT.BIN` and `FA.VFF` reveals that retail software decodes MJP video **100% in software on the ARM926EJ-S CPU**.
  - Retail code contains a dedicated software JPEG bitstream reader (`0x5cc90`), Huffman table traverser, integer IDCT with a 14-bit fixed-point cosine table (`0x1375b0`), and fixed-point YCbCr-to-RGB color converter (`0x5e900`).
  - Video presentation is performed by software rendering directly to a double-buffered 16-bit framebuffer (or blitting to the active scanout surface).

---

## 2. Container Format: MIAV (.MJP)

Retail `.MJP` files use a custom RIFF-like container format with the FourCC header magic `'MIAV'`.

### 2.1 File Header (64 bytes, little-endian)

| Offset | Type | Field | Description |
|---|---|---|---|
| `0x00..0x03` | `char[4]` | `magic` | Container signature: ASCII `"MIAV"` (`0x5641494d`) |
| `0x04..0x07` | `uint32_t` | `header_size` | Size of the file header: always `64` (`0x00000040`) |
| `0x08..0x0B` | `uint32_t` | `payload_size` | Total stream size in bytes following the header (`file_size - 64`) |
| `0x0C..0x0F` | `uint32_t` | `reserved` | Reserved, set to `0` |
| `0x10..0x13` | `uint32_t` | `frame_count` | Total number of video frames in the file |
| `0x14..0x15` | `uint16_t` | `fps` | Frame rate (typically `15` fps) |
| `0x16..0x17` | `uint16_t` | `audio_flags` | Audio format flag: `0x0004` = 16-bit PCM, `0x0001` = IMA ADPCM |
| `0x18..0x19` | `uint16_t` | `width` | Video frame width in pixels (e.g. `224`, `320`) |
| `0x1A..0x1B` | `uint16_t` | `height` | Video frame height in pixels (e.g. `112`, `160`) |
| `0x1C..0x3F` | `uint8_t[36]`| `padding` | Reserved / zero padding |

### 2.2 Chunk Stream Structure

Immediately following the 64-byte file header, audio and video data are interleaved in sequential chunks.

Each chunk begins with a **12-byte chunk header**:

| Offset | Type | Field | Description |
|---|---|---|---|
| `0x00..0x03` | `char[4]` | `chunk_id` | FourCC stream tag: `'00dc'`, `'01wb'`, `'02wb'` |
| `0x04..0x07` | `uint32_t` | `timestamp` | Frame sequence index / timestamp (`0, 1, 2, ...`) |
| `0x08..0x0B` | `uint32_t` | `chunk_size` | Byte length of the chunk payload |

*Note on Alignment*: If `chunk_size` is odd, a 1-byte padding byte is appended after the payload to maintain 2-byte word alignment.

#### Chunk Types
- `'00dc'`: Video compressed frame (Motion-JPEG).
- `'01wb'`: Audio channel 1 (Mono or Stereo Left).
- `'02wb'`: Audio channel 2 (Stereo Right, optional).

---

## 3. Video Stream: JPEG Encoding & Byte Swapping

### 3.1 16-Bit Word Byte Swapping
Every video chunk (`'00dc'`) contains a standard baseline JPEG image, but **stored in 16-bit halfword swapped byte order**:
- A standard JPEG bitstream beginning with `0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46` (`JFIF`) is written to disk as:
  `0xD8, 0xFF, 0xE0, 0xFF, 0x10, 0x00, 0x46, 0x4A` (`FJ`).
- **Unswapping Rule**: To decode each 16-bit word `W = (B1 << 8) | B0`, the decoder processes `B1` then `B0`.

### 3.2 JPEG Parameters
- **Type**: ISO/IEC 10918-1 Baseline Sequential DCT (`SOF0`, marker `0xFFC0`).
- **Precision**: 8 bits per sample.
- **Components**: 3 components ($Y, C_b, C_r$).
- **Subsampling**: 4:2:0 ($Y: 2\times2$, $C_b: 1\times1$, $C_r: 1\times1$).
- **MCU Dimensions**: $16 \times 16$ pixels (4 $Y$ blocks, 1 $C_b$ block, 1 $C_r$ block).
- **Restart Markers**: None (`RSTm` unused).

---

## 4. Audio Stream: PCM & IMA ADPCM

Retail MJP files support two audio encodings:
1. **Uncompressed PCM (`audio_flags = 0x0004`)**:
   - 16-bit signed PCM, little-endian.
   - At 15 fps and 22,050 Hz sample rate: $22050 / 15 = 1470$ samples per frame = $2940$ bytes per chunk.
2. **IMA ADPCM (`audio_flags = 0x0001`)**:
   - 4-byte header per chunk:
     - `int16_t initial_sample` (predicted sample value).
     - `uint8_t initial_index` (step table index, $0..88$).
     - `uint8_t reserved` ($0$).
   - 4-bit nibbles follow: low nibble first, high nibble second.
   - Decodes with standard DVI/IMA ADPCM step tables to 16-bit signed PCM.

---

## 5. Synchronization Strategy

1. Video and audio chunks are interleaved frame-by-frame: for each video frame $T$, chunk `'00dc'` is followed by audio chunk `'01wb'`.
2. Audio playback is pushed to the hardware audio buffer / DMA.
3. The video presentation timer synchronizes to audio elapsed samples or the 10 kHz hardware clock (`vflash_clock_ticks()`), presenting the newly decoded frame every $10000 / \text{fps}$ ticks ($666$ ticks at 15 fps).

---

## 6. Proposed Software Decoder Architecture

Because native hardware decoding does not exist on the Zevio SoC:
- **Baseline JPEG Engine**:
  - A compact, freestanding, bounded-memory software decoder (`picojpeg` / custom MCU-streaming decoder) requiring **$< 3$ KB** of working state memory.
  - Zero dynamic heap allocation during playback loop.
  - Streaming MCU-by-MCU decoding directly into the target 16-bit BGR555 framebuffer, eliminating intermediate full-frame RGB buffers.
- **16-bit Byte Unswapping**:
  - Handled on-the-fly during bitstream reading or during sector DMA loading with optimized 32-bit word rotation instructions (`eor`, `ror #8`, `bic`).
- **HAL Integration**:
  - Integrates cleanly with `vflash_fs_*` for streaming from disc, `vflash_audio_*` for sound output, and `vflash_video_*` / GPU HAL for display scanout.

