"""Render a labeled atlas of every printable ASCII glyph using the SDK's C font."""
import argparse
import ctypes
from pathlib import Path
import struct
import tempfile
from host_renderer import Framebuffer, build_library, to_rgb


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='destination PPM image (host preview, not emulator output)')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='font-preview-') as temp:
        library = build_library(Path(temp) / 'font.so')
        pixels = (ctypes.c_uint16 * (320 * 240))()
        fb = Framebuffer(pixels, 320, 240, 320, len(pixels), 1)
        library.draw_clear(ctypes.byref(fb), 0x101820)
        library.draw_text(ctypes.byref(fb), 12, 8, b'ASCII 0x20 - 0x7E', 0xFFFFFF, 2)
        for index, code in enumerate(range(0x20, 0x7F)):
            x, y = 16 + index % 16 * 18, 40 + index // 16 * 30
            library.draw_text(ctypes.byref(fb), x, y, f'{code:02X}'.encode(), 0x60E0B0, 1)
            library.draw_text(ctypes.byref(fb), x, y + 10, bytes([code]), 0xFFFFFF, 2)
        native = struct.pack('<76800H', *pixels)
        args.output.write_bytes(b'P6\n320 240\n255\n' + to_rgb(native))
    print(f'Host font atlas: {args.output.resolve()} (95 glyphs, including blank space at 20)')


if __name__ == '__main__':
    main()
