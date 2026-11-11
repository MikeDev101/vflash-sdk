"""Compile and call the same CPU drawing implementation on the host."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT = 320, 240


class Framebuffer(ctypes.Structure):
    _fields_ = [("pixels", ctypes.POINTER(ctypes.c_uint16)), ("width", ctypes.c_uint32),
                ("height", ctypes.c_uint32), ("stride", ctypes.c_uint32),
                ("capacity_pixels", ctypes.c_uint32), ("pixel_format", ctypes.c_uint32)]


def build_library(output):
    compiler = os.environ.get("VFLASH_HOST_CC") or shutil.which("cc")
    if not compiler:
        raise ValueError("host rendering tests require a native C compiler (cc or VFLASH_HOST_CC)")
    subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
                    "-I", str(ROOT / "include"),
                    *[str(ROOT / name) for name in ("src/graphics/draw.c", "src/graphics/font.c",
                        "examples/common/demo.c", "examples/animation/animation.c")],
                    "-o", str(output)], check=True, capture_output=True, timeout=30)
    library = ctypes.CDLL(str(output))
    fb_pointer = ctypes.POINTER(Framebuffer)
    library.draw_color.argtypes = [ctypes.c_uint32]
    library.draw_color.restype = ctypes.c_uint16
    library.draw_valid.argtypes = [fb_pointer]
    library.draw_valid.restype = ctypes.c_int
    library.draw_clear.argtypes = [fb_pointer, ctypes.c_uint32]
    library.draw_pixel.argtypes = [fb_pointer, ctypes.c_int32, ctypes.c_int32, ctypes.c_uint32]
    library.draw_rect.argtypes = [fb_pointer, *([ctypes.c_int32] * 4), ctypes.c_uint32]
    library.draw_text.argtypes = [fb_pointer, ctypes.c_int32, ctypes.c_int32, ctypes.c_char_p,
                                  ctypes.c_uint32, ctypes.c_uint32]
    library.demo_render.argtypes = [fb_pointer]
    library.animation_init.argtypes = [ctypes.POINTER(AnimationState)]
    library.animation_step.argtypes = [ctypes.POINTER(AnimationState)]
    library.animation_render.argtypes = [fb_pointer, ctypes.POINTER(AnimationState), ctypes.c_uint32, ctypes.c_uint32]
    for name in ("draw_clear", "draw_pixel", "draw_rect", "draw_text", "demo_render",
                 "animation_init", "animation_step", "animation_render"):
        getattr(library, name).restype = None
    return library


class AnimationState(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int32), ("y", ctypes.c_int32), ("dx", ctypes.c_int32), ("dy", ctypes.c_int32)]


def to_rgb(native):
    """Decode the emulator's documented xBGR1555 scanout, not drawing logic."""
    if len(native) % 2:
        raise ValueError("native framebuffer length must be even")
    rgb = bytearray()
    for (value,) in struct.iter_unpack("<H", native):
        for shift in (0, 5, 10):
            channel = (value >> shift) & 31
            rgb.append((channel << 3) | (channel >> 2))
    return bytes(rgb)


def render_reference(output, animation=None):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="renderer-", dir=output) as temp:
        library = build_library(Path(temp) / "draw.so")
        pixels = (ctypes.c_uint16 * (WIDTH * HEIGHT))()
        fb = Framebuffer(pixels, WIDTH, HEIGHT, WIDTH, WIDTH * HEIGHT, 1)
        if animation is None:
            library.demo_render(ctypes.byref(fb))
        else:
            state = AnimationState(animation["x"], animation["y"], 2, 3)
            library.animation_render(ctypes.byref(fb), ctypes.byref(state), animation["fps"], animation["shown_us"])
        # Explicit little endian; do not depend on host byte order.
        native = struct.pack(f"<{len(pixels)}H", *pixels)
    rgb = to_rgb(native)
    (output / "frame.bgr555").write_bytes(native)
    (output / "frame.ppm").write_bytes(f"P6\n{WIDTH} {HEIGHT}\n255\n".encode() + rgb)
    metadata = dict(width=WIDTH, height=HEIGHT, stride=WIDTH, pixel_format="xBGR1555",
                    native_sha256=hashlib.sha256(native).hexdigest(),
                    rgb_sha256=hashlib.sha256(rgb).hexdigest())
    (output / "reference.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return native, rgb, metadata


def read_ppm(path):
    # FlashEm emits exactly this simple P6 header, with no comments.
    with Path(path).open("rb") as stream:
        if stream.readline() != b"P6\n":
            raise ValueError("expected FlashEm P6 screenshot")
        dimensions = stream.readline().split()
        if len(dimensions) != 2 or stream.readline() != b"255\n":
            raise ValueError("invalid screenshot header")
        width, height = map(int, dimensions)
        if not 0 < width <= 1024 or not 0 < height <= 1024:
            raise ValueError("screenshot dimensions outside inspection limits")
        rgb = stream.read(width * height * 3 + 1)
        if len(rgb) != width * height * 3:
            raise ValueError("screenshot size mismatch")
    return width, height, rgb


def render_font_atlas(output):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="font-preview-") as temp:
        library = build_library(Path(temp) / "font.so")
        pixels = (ctypes.c_uint16 * (WIDTH * HEIGHT))()
        fb = Framebuffer(pixels, WIDTH, HEIGHT, WIDTH, len(pixels), 1)
        library.draw_clear(ctypes.byref(fb), 0x101820)
        library.draw_text(ctypes.byref(fb), 12, 8, b"ASCII 0x20 - 0x7E", 0xFFFFFF, 2)
        for index, code in enumerate(range(0x20, 0x7F)):
            x, y = 16 + index % 16 * 18, 40 + index // 16 * 30
            library.draw_text(ctypes.byref(fb), x, y, f"{code:02X}".encode(), 0x60E0B0, 1)
            library.draw_text(ctypes.byref(fb), x, y + 10, bytes([code]), 0xFFFFFF, 2)
        native = struct.pack(f"<{WIDTH * HEIGHT}H", *pixels)
        output.write_bytes(f"P6\n{WIDTH} {HEIGHT}\n255\n".encode() + to_rgb(native))
    print(f"Host font atlas: {output.resolve()} (95 glyphs, including blank space at 20)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="output directory (or destination PPM with --font-atlas)")
    parser.add_argument("--font-atlas", action="store_true", help="render a labeled printable ASCII font atlas PPM")
    args = parser.parse_args()
    try:
        if args.font_atlas:
            render_font_atlas(args.output)
        else:
            _, _, metadata = render_reference(args.output)
            print(json.dumps(metadata, indent=2))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Host render failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
