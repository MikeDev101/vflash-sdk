import ctypes
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_renderer import AnimationState, Framebuffer, build_library


class AnimationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.lib = build_library(Path(cls.temp.name) / "animation.so")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_motion_stays_in_bounds_and_bounces(self):
        state = AnimationState()
        self.lib.animation_init(ctypes.byref(state))
        x_directions, y_directions = set(), set()
        for _ in range(10000):
            self.lib.animation_step(ctypes.byref(state))
            self.assertTrue(16 <= state.x <= 280)
            self.assertTrue(84 <= state.y <= 180)
            x_directions.add(state.dx)
            y_directions.add(state.dy)
        self.assertEqual(x_directions, {-2, 2})
        self.assertEqual(y_directions, {-3, 3})

    def test_frames_change_without_writing_past_buffer(self):
        count = 320 * 240
        storage = (ctypes.c_uint16 * (count + 32))(*([0xA55A] * (count + 32)))
        pointer = ctypes.cast(ctypes.byref(storage, 32), ctypes.POINTER(ctypes.c_uint16))
        fb = Framebuffer(pointer, 320, 240, 320, count, 1)
        state = AnimationState()
        self.lib.animation_init(ctypes.byref(state))
        self.lib.animation_render(ctypes.byref(fb), ctypes.byref(state), 60, 3500)
        first = bytes(storage)
        self.lib.animation_step(ctypes.byref(state))
        self.lib.animation_render(ctypes.byref(fb), ctypes.byref(state), 60, 3500)
        self.assertNotEqual(bytes(storage), first)
        self.assertEqual(list(storage[:16]), [0xA55A] * 16)
        self.assertEqual(list(storage[-16:]), [0xA55A] * 16)
        self.assertEqual(pointer[(state.y + 4) * 320 + state.x + 4], self.lib.draw_color(0xFFCC40))


if __name__ == "__main__":
    unittest.main()
