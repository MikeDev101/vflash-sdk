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


if __name__ == "__main__":
    unittest.main()
