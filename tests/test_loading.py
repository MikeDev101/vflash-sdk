import ctypes
from pathlib import Path
import tempfile
import unittest

SDK_ROOT = Path(__file__).resolve().parents[1]


class LoadingScreenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # We can compile loading.c, draw.c, font.c into a shared library for host testing
        import subprocess

        cls.temp = tempfile.TemporaryDirectory(prefix="vflash-test-loading-")
        so_path = Path(cls.temp.name) / "libloading.so"

        srcs = [
            SDK_ROOT / "src/platform/loading.c",
            SDK_ROOT / "src/graphics/draw.c",
            SDK_ROOT / "src/graphics/font.c",
        ]

        # Provide stub implementations of video and clock for host test
        stub_c = Path(cls.temp.name) / "stubs.c"
        stub_c.write_text("""
#include "vflash/video.h"
#include "vflash/clock.h"
static uint16_t s_mock_pixels[320 * 240];
int vflash_video_init(vflash_framebuffer_t *fb) {
    if (!fb) return 0;
    fb->pixels = s_mock_pixels;
    fb->width = 320;
    fb->height = 240;
    fb->stride = 320;
    fb->capacity_pixels = 320 * 240;
    fb->pixel_format = VFLASH_BGR555;
    return 1;
}
int vflash_video_present(const vflash_framebuffer_t *fb) { (void)fb; return 1; }
int vflash_video_backbuffer(vflash_framebuffer_t *fb) { return vflash_video_init(fb); }
static uint16_t s_mock_ticks = 0;
uint16_t vflash_clock_ticks(void) {
    s_mock_ticks += 50;
    return s_mock_ticks;
}
""")

        cmd = [
            "gcc", "-shared", "-fPIC", "-O2",
            f"-I{SDK_ROOT}/include",
            "-o", str(so_path),
            str(stub_c),
            *[str(s) for s in srcs]
        ]
        subprocess.run(cmd, check=True)

        cls.lib = ctypes.CDLL(str(so_path))
        cls.lib.vflash_loading_init.restype = None
        cls.lib.vflash_loading_init.argtypes = []
        cls.lib.vflash_loading_set_progress.restype = None
        cls.lib.vflash_loading_set_progress.argtypes = [ctypes.c_uint]
        cls.lib.vflash_loading_set_status.restype = None
        cls.lib.vflash_loading_set_status.argtypes = [ctypes.c_char_p]
        cls.lib.vflash_loading_render.restype = None
        cls.lib.vflash_loading_render.argtypes = []
        cls.lib.vflash_loading_finish.restype = None
        cls.lib.vflash_loading_finish.argtypes = []
        cls.lib.vflash_loading_get_progress.restype = ctypes.c_uint
        cls.lib.vflash_loading_get_progress.argtypes = []
        cls.lib.vflash_loading_get_status.restype = ctypes.c_char_p
        cls.lib.vflash_loading_get_status.argtypes = []

        cls.lib.vflash_loading_set_title.restype = None
        cls.lib.vflash_loading_set_title.argtypes = [ctypes.c_char_p, ctypes.c_char_p]

        class LoadingConfig(ctypes.Structure):
            _fields_ = [
                ("title", ctypes.c_char_p),
                ("subtitle", ctypes.c_char_p),
                ("caption", ctypes.c_char_p),
                ("copyright", ctypes.c_char_p),
            ]
        cls.LoadingConfig = LoadingConfig
        cls.lib.vflash_loading_init_ex.restype = None
        cls.lib.vflash_loading_init_ex.argtypes = [ctypes.POINTER(LoadingConfig)]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_initialization_defaults(self):
        self.lib.vflash_loading_init()
        self.assertEqual(self.lib.vflash_loading_get_progress(), 0)
        status = self.lib.vflash_loading_get_status()
        self.assertTrue(b"Initializing" in status)

    def test_progress_clamping(self):
        self.lib.vflash_loading_set_progress(42)
        self.assertEqual(self.lib.vflash_loading_get_progress(), 42)

        self.lib.vflash_loading_set_progress(100)
        self.assertEqual(self.lib.vflash_loading_get_progress(), 100)

        self.lib.vflash_loading_set_progress(150)
        self.assertEqual(self.lib.vflash_loading_get_progress(), 100)

    def test_status_updates(self):
        self.lib.vflash_loading_set_status(b"Mounting V.DISC")
        self.assertEqual(self.lib.vflash_loading_get_status(), b"Mounting V.DISC")

        # Test long status string truncation
        long_str = b"This is a very long status message that exceeds thirty-two characters"
        self.lib.vflash_loading_set_status(long_str)
        status = self.lib.vflash_loading_get_status()
        self.assertLessEqual(len(status), 31)
        self.assertTrue(long_str.startswith(status))

        # Test NULL status
        self.lib.vflash_loading_set_status(None)
        self.assertEqual(self.lib.vflash_loading_get_status(), b"")

    def test_custom_config_and_titles(self):
        # Test configuring with custom title and subtitle via init_ex
        cfg = self.LoadingConfig(
            title=b"MY GAME",
            subtitle=b"Action Adventure",
            caption=b"Custom Engine",
            copyright=b"(C) 2026 Studio"
        )
        self.lib.vflash_loading_init_ex(ctypes.byref(cfg))
        self.assertEqual(self.lib.vflash_loading_get_progress(), 0)

        # Test dynamic title update
        self.lib.vflash_loading_set_title(b"NEW TITLE", b"New Subtitle")
        self.lib.vflash_loading_render()

    def test_render_and_finish_lifecycle(self):
        self.lib.vflash_loading_init()
        for p in (10, 30, 50, 75, 90):
            self.lib.vflash_loading_set_progress(p)
            self.lib.vflash_loading_render()
            self.assertEqual(self.lib.vflash_loading_get_progress(), p)

        self.lib.vflash_loading_finish()
        self.assertEqual(self.lib.vflash_loading_get_progress(), 100)
        self.assertEqual(self.lib.vflash_loading_get_status(), b"Ready")


if __name__ == "__main__":
    unittest.main()
