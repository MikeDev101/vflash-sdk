import ctypes
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from host_renderer import build_library


class FontTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='vflash-font-')
        cls.library = build_library(Path(cls.temp.name) / 'font.so')
        cls.library.font_glyph.argtypes = [ctypes.c_ubyte]
        cls.library.font_glyph.restype = ctypes.POINTER(ctypes.c_uint8)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def glyph(self, code):
        return tuple(self.library.font_glyph(code)[:7])

    def test_all_printable_ascii_has_a_distinct_glyph(self):
        glyphs = [self.glyph(code) for code in range(0x20, 0x7F)]
        self.assertEqual(glyphs[0], (0,) * 7)
        self.assertEqual(len(set(glyphs)), 95)
        for code, glyph in zip(range(0x21, 0x7F), glyphs[1:]):
            with self.subTest(character=chr(code)):
                self.assertTrue(any(glyph))
                self.assertTrue(all(0 <= row < 32 for row in glyph))

    def test_controls_and_non_ascii_bytes_use_question_mark(self):
        fallback = self.glyph(ord('?'))
        for code in (*range(0x20), *range(0x7F, 0x100)):
            with self.subTest(byte=code):
                self.assertEqual(self.glyph(code), fallback)

    def test_lowercase_letters_are_not_uppercase_aliases(self):
        for code in range(ord('a'), ord('z') + 1):
            with self.subTest(letter=chr(code)):
                self.assertNotEqual(self.glyph(code), self.glyph(code - 32))
