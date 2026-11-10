import ctypes
from pathlib import Path
import random
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_renderer import Framebuffer, build_library


class DrawTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="vflash-host-draw-")
        cls.lib = build_library(Path(cls.temp.name) / "draw.so")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.width, self.height, self.stride = 19, 13, 24
        self.guard, self.canary = 16, 0xA55A
        self.count = self.stride * self.height
        self.storage = (ctypes.c_uint16 * (self.count + 2 * self.guard))()
        for i in range(len(self.storage)):
            self.storage[i] = self.canary
        pointer = ctypes.cast(ctypes.byref(self.storage, 2 * self.guard), ctypes.POINTER(ctypes.c_uint16))
        self.fb = Framebuffer(pointer, self.width, self.height, self.stride, self.count, 1)

    def tearDown(self):
        self.assertEqual(list(self.storage[:self.guard]), [self.canary] * self.guard)
        self.assertEqual(list(self.storage[-self.guard:]), [self.canary] * self.guard)
        for y in range(self.height):
            self.assertEqual(list(self.storage[self.guard + y * self.stride + self.width:
                                               self.guard + (y + 1) * self.stride]),
                             [self.canary] * (self.stride - self.width))

    def pixel(self, x, y):
        return self.fb.pixels[y * self.stride + x]

    def test_color_channels_and_opacity(self):
        for rgb, native in ((0xFF0000, 0x001F), (0x00FF00, 0x03E0), (0x0000FF, 0x7C00),
                            (0xFFFFFF, 0x7FFF), (0, 0), (0x101820, 0x1062)):
            self.assertEqual(self.lib.draw_color(rgb), native)

    def test_clear_stride_and_pixel_edges(self):
        self.lib.draw_clear(ctypes.byref(self.fb), 0)
        self.lib.draw_pixel(ctypes.byref(self.fb), 18, 12, 0xFF0000)
        for x, y in ((-1, 0), (19, 0), (0, -1), (0, 13), (-2147483648, 2147483647)):
            self.lib.draw_pixel(ctypes.byref(self.fb), x, y, 0xFFFFFF)
        self.assertEqual(self.pixel(18, 12), 0x1F)
        self.assertEqual(sum(self.pixel(x, y) != 0 for y in range(13) for x in range(19)), 1)

    def test_rect_clipping_and_empty(self):
        self.lib.draw_clear(ctypes.byref(self.fb), 0)
        self.lib.draw_rect(ctypes.byref(self.fb), -2, -1, 5, 4, 0x00FF00)
        for y in range(13):
            for x in range(19):
                self.assertEqual(self.pixel(x, y), 0x3E0 if x < 3 and y < 3 else 0)
        before = bytes(self.storage)
        for width, height in ((0, 5), (5, 0), (-1, 2), (2, -1)):
            self.lib.draw_rect(ctypes.byref(self.fb), 0, 0, width, height, 0xFFFFFF)
        self.assertEqual(bytes(self.storage), before)

    def test_extreme_and_random_clipping(self):
        rng = random.Random(926)
        values = [-2147483648, -100, -1, 0, 1, 15, 100, 2147483647]
        for _ in range(1000):
            x, y, w, h = [rng.choice(values) for _ in range(4)]
            self.lib.draw_rect(ctypes.byref(self.fb), x, y, w, h, rng.getrandbits(24))

    def test_glyph_pixels_transparency_and_clipping(self):
        self.lib.draw_clear(ctypes.byref(self.fb), 0x0000FF)
        self.lib.draw_text(ctypes.byref(self.fb), 1, 1, b"H", 0xFFFFFF, 1)
        self.assertEqual(self.pixel(1, 1), 0x7FFF)
        self.assertEqual(self.pixel(5, 1), 0x7FFF)
        self.assertEqual(self.pixel(3, 1), 0x7C00)  # transparent glyph interior
        self.assertEqual(self.pixel(3, 4), 0x7FFF)  # H crossbar
        self.lib.draw_text(ctypes.byref(self.fb), -3, -2, b"Hello, World!", 0xFF0000, 2)
        self.lib.draw_text(ctypes.byref(self.fb), 17, 12, b"!", 0xFFFFFF, 3)
        self.lib.draw_text(ctypes.byref(self.fb), -2147483648, -2147483648, b"H", 0, 16)
        self.lib.draw_text(ctypes.byref(self.fb), 2147483647, 2147483647, b"H", 0, 16)

    def test_invalid_descriptors_and_text(self):
        before = bytes(self.storage)
        for field, value in (("stride", 1), ("capacity_pixels", 1), ("pixel_format", 99),
                             ("width", 0), ("height", 0xFFFFFFFF)):
            old = getattr(self.fb, field)
            setattr(self.fb, field, value)
            self.assertFalse(self.lib.draw_valid(ctypes.byref(self.fb)))
            self.lib.draw_clear(ctypes.byref(self.fb), 0)
            setattr(self.fb, field, old)
        for scale in (0, 17, 0xFFFFFFFF):
            self.lib.draw_text(ctypes.byref(self.fb), 0, 0, b"H", 0, scale)
        self.lib.draw_text(ctypes.byref(self.fb), 0, 0, None, 0, 1)
        self.lib.draw_clear(None, 0)
        self.assertEqual(bytes(self.storage), before)

    def test_demo_colors_and_determinism(self):
        pixels = (ctypes.c_uint16 * (320 * 240))()
        fb = Framebuffer(pixels, 320, 240, 320, len(pixels), 1)
        self.lib.demo_render(ctypes.byref(fb))
        first = bytes(pixels)
        for x, native in ((50, 0x1F), (150, 0x3E0), (250, 0x7C00)):
            self.assertEqual(pixels[130 * 320 + x], native)
        self.assertEqual(pixels[107 * 320 + 37], 0x7FFF)
        self.assertEqual(pixels[0], 0x1062)
        self.lib.demo_render(ctypes.byref(fb))
        self.assertEqual(bytes(pixels), first)


if __name__ == "__main__":
    unittest.main()
