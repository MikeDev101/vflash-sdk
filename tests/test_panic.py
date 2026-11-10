"""Host bounds checks for the emergency renderer; hardware traps use panic_smoke.py."""
import ctypes
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from panic_smoke import RECORD, WORDS, load_renderer, reference_pixels


class PanicRendererTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.library = load_renderer(Path(cls.temp.name) / 'panic.so')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_arm_record_abi_matches_assembly_and_dump_reader(self):
        self.assertEqual(RECORD.size, 180)
        self.assertEqual(WORDS.index('secondary_reason') * 4, 104)
        self.assertEqual(WORDS.index('secondary_pc') * 4, 108)

    def test_long_message_and_hex_values_stay_within_framebuffer(self):
        for reason in (1, 2, 3, 4):
            words = [0xFFFFFFFF] * 29
            words[2] = reason
            saved = RECORD.pack(*words, b'W' * 63 + b'\0')
            pixels = reference_pixels(saved, self.library)
            self.assertEqual(len(pixels), 153600)
            self.assertEqual(set(struct.unpack('<76800H', pixels)), {0x6000, 0x7FFF})

    def test_render_is_deterministic_and_reflects_pc(self):
        words = [0] * 29
        words[2] = 3
        first = reference_pixels(RECORD.pack(*words, b''), self.library)
        self.assertEqual(first, reference_pixels(RECORD.pack(*words, b''), self.library))
        words[4] = 0x10C01234
        second = reference_pixels(RECORD.pack(*words, b''), self.library)
        self.assertNotEqual(first, second)
        # A PC change must not affect the title or the rest of the register rows.
        self.assertEqual(first[:60*320*2], second[:60*320*2])
        self.assertEqual(first[74*320*2:], second[74*320*2:])
