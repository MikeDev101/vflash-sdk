"""Regression and unit tests for V.Flash MJP video codec and container."""

import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from mjp_encode import (
    MIAV_MAGIC,
    HEADER_SIZE,
    CHUNK_VIDEO,
    CHUNK_AUDIO,
    AUDIO_FMT_NONE,
    AUDIO_FMT_PCM16,
    AUDIO_FMT_ADPCM,
    swap_bytes_16,
    unswap_bytes_16,
    encode_ima_adpcm,
    decode_ima_adpcm,
    generate_synthetic_frame,
    generate_synthetic_audio,
    encode_mjp,
    verify_mjp,
)


class MjpContainerTests(unittest.TestCase):
    def test_byte_swap_symmetry(self):
        original = bytes([0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC])
        swapped = swap_bytes_16(original)
        self.assertEqual(swapped, bytes([0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A]))
        unswapped = unswap_bytes_16(swapped)
        self.assertEqual(unswapped, original)

    def test_byte_swap_odd_length_padding(self):
        original = bytes([0x12, 0x34, 0x56])
        swapped = swap_bytes_16(original)
        self.assertEqual(len(swapped), 4)
        self.assertEqual(swapped, bytes([0x34, 0x12, 0x00, 0x56]))

    def test_ima_adpcm_roundtrip(self):
        # 201 samples: 1 initial sample + 200 samples in 100 bytes (200 nibbles)
        import math
        sample_rate = 22050
        freq = 440
        samples = [int(10000 * math.sin(2 * math.pi * freq * i / sample_rate)) for i in range(201)]
        encoded = encode_ima_adpcm(samples)
        self.assertGreater(len(encoded), 4)
        decoded = decode_ima_adpcm(encoded)
        self.assertEqual(len(decoded), len(samples))
        self.assertEqual(decoded[0], samples[0])
        # Check reconstruction accuracy after initial filter adaptation (first 20 samples)
        errs = [abs(o - d) for o, d in zip(samples[20:], decoded[20:])]
        self.assertLess(sum(errs) / len(errs), 150)
        self.assertLess(max(errs), 500)

    def test_encode_and_verify_synthetic_pcm(self):
        frames = []
        for i in range(5):
            img = generate_synthetic_frame(i, 5, 224, 112)
            bio = io.BytesIO()
            img.save(bio, format="JPEG", quality=70, subsampling="4:2:0")
            frames.append(bio.getvalue())

        audio = generate_synthetic_audio(5, fps=15, sample_rate=22050)
        data = encode_mjp(frames, audio, 224, 112, fps=15, audio_fmt=AUDIO_FMT_PCM16)

        info = verify_mjp(data)
        self.assertEqual(info["frame_count"], 5)
        self.assertEqual(info["fps"], 15)
        self.assertEqual(info["width"], 224)
        self.assertEqual(info["height"], 112)
        self.assertEqual(info["audio_fmt"], "PCM16")
        self.assertEqual(info["audio_chunks"], 5)

    def test_encode_and_verify_synthetic_adpcm(self):
        frames = []
        for i in range(4):
            img = generate_synthetic_frame(i, 4, 320, 160)
            bio = io.BytesIO()
            img.save(bio, format="JPEG", quality=75, subsampling="4:2:0")
            frames.append(bio.getvalue())

        audio = generate_synthetic_audio(4, fps=15, sample_rate=22050)
        data = encode_mjp(frames, audio, 320, 160, fps=15, audio_fmt=AUDIO_FMT_ADPCM)

        info = verify_mjp(data)
        self.assertEqual(info["frame_count"], 4)
        self.assertEqual(info["width"], 320)
        self.assertEqual(info["height"], 160)
        self.assertEqual(info["audio_fmt"], "ADPCM")
        self.assertEqual(info["audio_chunks"], 4)

    def test_corrupted_headers(self):
        # Truncated
        with self.assertRaises(ValueError):
            verify_mjp(b"MIAV" + b"\x00" * 10)

        # Invalid magic
        bad_magic = bytearray(64)
        struct.pack_into("<I", bad_magic, 0, 0x12345678)
        with self.assertRaises(ValueError):
            verify_mjp(bad_magic)

    def test_repeated_playback_stream_integrity(self):
        """Simulate looping playback by parsing stream twice."""
        frames = []
        for i in range(3):
            img = Image.new("RGB", (64, 64), (i * 40, i * 60, i * 80))
            bio = io.BytesIO()
            img.save(bio, format="JPEG", quality=80, subsampling="4:2:0")
            frames.append(bio.getvalue())

        data = encode_mjp(frames, b"", 64, 64, fps=15, audio_fmt=AUDIO_FMT_NONE)

        for iteration in range(3):
            info = verify_mjp(data)
            self.assertEqual(info["frame_count"], 3)

    def test_picojpeg_color_to_grayscale_neutral_chroma_transition(self):
        """Verify PicoJPEG neutral-chroma (c == 128) skip in PJPG_YH2V2 never leaks stale chroma."""
        import ctypes
        import subprocess

        repo_root = Path(__file__).resolve().parents[1]
        c_src = repo_root / "src" / "graphics" / "picojpeg.c"
        inc_dir = repo_root / "include"

        with tempfile.TemporaryDirectory() as tmpdir:
            so_path = Path(tmpdir) / "libpicojpeg_test.so"
            wrapper_c = Path(tmpdir) / "wrapper.c"
            wrapper_c.write_text(
                """
                #include <stdint.h>
                #include <string.h>
                #include "vflash/picojpeg.h"

                typedef struct {
                    const uint8_t *data;
                    uint32_t size;
                    uint32_t pos;
                } mem_buf_t;

                static unsigned char read_cb(unsigned char *pBuf, unsigned char buf_size,
                                             unsigned char *pBytes_actually_read, void *pCallback_data) {
                    mem_buf_t *m = (mem_buf_t *)pCallback_data;
                    uint32_t avail = (m->pos < m->size) ? (m->size - m->pos) : 0;
                    uint32_t n = (avail < buf_size) ? avail : buf_size;
                    if (n > 0) {
                        memcpy(pBuf, m->data + m->pos, n);
                        m->pos += n;
                    }
                    *pBytes_actually_read = (unsigned char)n;
                    return 0;
                }

                int decode_jpeg_32x16(const uint8_t *jpeg_data, uint32_t jpeg_size, uint8_t *out_rgb) {
                    mem_buf_t mb = { jpeg_data, jpeg_size, 0 };
                    pjpeg_image_info_t info;
                    if (pjpeg_decode_init(&info, read_cb, &mb, 0) != 0) return -1;
                    if (info.m_width != 32 || info.m_height != 16) return -2;

                    for (int mcu_x = 0; mcu_x < info.m_MCUSPerRow; ++mcu_x) {
                        if (pjpeg_decode_mcu() != 0) return -3;
                        for (int by = 0; by < 2; ++by) {
                            for (int bx = 0; bx < 2; ++bx) {
                                int blk_off = (by * 2 + bx) * 64;
                                for (int py = 0; py < 8; ++py) {
                                    for (int px = 0; px < 8; ++px) {
                                        int x = mcu_x * 16 + bx * 8 + px;
                                        int y = by * 8 + py;
                                        int idx = (y * 32 + x) * 3;
                                        int s = blk_off + py * 8 + px;
                                        out_rgb[idx + 0] = info.m_pMCUBufR[s];
                                        out_rgb[idx + 1] = info.m_pMCUBufG[s];
                                        out_rgb[idx + 2] = info.m_pMCUBufB[s];
                                    }
                                }
                            }
                        }
                    }
                    return 0;
                }
                """,
                encoding="utf-8",
            )
            subprocess.run(
                [
                    "gcc",
                    "-O3",
                    "-shared",
                    "-fPIC",
                    f"-I{inc_dir}",
                    str(c_src),
                    str(wrapper_c),
                    "-o",
                    str(so_path),
                ],
                check=True,
            )

            lib = ctypes.CDLL(str(so_path))
            lib.decode_jpeg_32x16.argtypes = [
                ctypes.c_char_p,
                ctypes.c_uint32,
                ctypes.POINTER(ctypes.c_uint8),
            ]
            lib.decode_jpeg_32x16.restype = ctypes.c_int

            # Build a 32x16 4:2:0 JPEG: left 16x16 MCU = saturated red (255, 0, 0),
            # right 16x16 MCU = neutral gray (200, 200, 200) where Cb=128, Cr=128
            img = Image.new("RGB", (32, 16))
            for y in range(16):
                for x in range(16):
                    img.putpixel((x, y), (255, 0, 0))
                for x in range(16, 32):
                    img.putpixel((x, y), (200, 200, 200))

            bio = io.BytesIO()
            img.save(bio, format="JPEG", quality=95, subsampling="4:2:0")
            jpeg_bytes = bio.getvalue()

            out_buf = (ctypes.c_uint8 * (32 * 16 * 3))()
            rc = lib.decode_jpeg_32x16(jpeg_bytes, len(jpeg_bytes), out_buf)
            self.assertEqual(rc, 0)

            # Verify left MCU is saturated red
            for y in range(16):
                for x in range(16):
                    idx = (y * 32 + x) * 3
                    r, g, b = out_buf[idx], out_buf[idx + 1], out_buf[idx + 2]
                    self.assertGreater(r, 240)
                    self.assertLess(g, 15)
                    self.assertLess(b, 15)

            # Verify right MCU is strictly neutral gray (R == G == B ~= 200), zero chroma bleed
            for y in range(16):
                for x in range(16, 32):
                    idx = (y * 32 + x) * 3
                    r, g, b = out_buf[idx], out_buf[idx + 1], out_buf[idx + 2]
                    self.assertEqual(r, g)
                    self.assertEqual(g, b)
                    self.assertAlmostEqual(r, 200, delta=3)


if __name__ == "__main__":
    unittest.main()
