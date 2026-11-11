"""Unified GPU 2D/3D and SDL2 renderer acceptance suite for pinned FlashEm."""
import argparse
import concurrent.futures
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from host_renderer import read_ppm, to_rgb

ROOT = Path(__file__).resolve().parents[1]

MULTI_VIEW_TESTS = (
    "tiled",
    "frame",
    "sprite",
    "transparent",
    "alpha",
    "modes",
    "renderer",
    "lifecycle",
    "sdl",
    "sdl2",
    "triangle",
    "triangle-depth",
    "triangle-tex",
    "cube",
)
ALL_TESTS = ("software", "mmu", "native", *MULTI_VIEW_TESTS)


def tiled_offset(x, y):
    return (y // 8) * 8192 + (x // 32) * 256 + (y % 8) * 32 + x % 32


def draw_color(rgb):
    r = (rgb >> 19) & 31
    g = (rgb >> 11) & 31
    b = (rgb >> 3) & 31
    return (b << 10) | (g << 5) | r


def load_symbols(elf):
    out = subprocess.check_output(["arm-none-eabi-nm", "--defined-only", str(elf)], text=True)
    return {p[2]: int(p[0], 16) for line in out.splitlines() if len(p := line.split()) == 3}


def run_emulator_smoke(emulator, bios, elf, disc, out, target=None, trace_video=False, timeout=150):
    if target:
        subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", target],
                       check=True, stdout=subprocess.DEVNULL)
    cmd = [
        sys.executable, str(ROOT / "tools/emulator_smoke.py"),
        "--emulator", str(emulator), "--bios", str(bios),
        "--elf", str(elf), "--disc", str(disc),
        "--out", str(out),
    ]
    if trace_video:
        cmd.append("--trace-video")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, timeout=timeout)


def verify_scanout_sequence(out, sequence):
    text = (out / "run.log").read_text(errors="replace")
    addresses = [int(v, 16) for v in re.findall(r"layer 0 address ([0-9A-Fa-f]{8})", text)]
    seq = list(sequence)
    for address in addresses:
        if seq and address == seq[0]:
            seq.pop(0)
    assert not seq, "scanout did not visit A, B, then linear recovery"
    return addresses


def software_fill_reference():
    code = r'''#include "vflash/gpu.h"
#include <stdio.h>
int main(void) {
 uint16_t pixels[256];
 vflash_framebuffer_t fb={pixels,32,8,32,256,VFLASH_BGR555};
 vflash_gpu_t gpu; vflash_gpu_surface_t surface;
 if(vflash_gpu_init(&gpu,VFLASH_GPU_SOFTWARE) ||
    vflash_gpu_surface_register(&surface,&fb,0,0) ||
    vflash_gpu_fill_rect(&gpu,&surface,0,0,32,8,0xFF0000)) return 1;
 return fwrite(pixels,sizeof(pixels),1,stdout)==1 ? 0:1;
}'''
    with tempfile.TemporaryDirectory(prefix="gpu-reference-") as temp:
        source = Path(temp) / "reference.c"
        source.write_text(code)
        exe = Path(temp) / "reference"
        compiler = os.environ.get("VFLASH_HOST_CC") or shutil.which("cc")
        if not compiler:
            raise ValueError("native comparison requires a host C compiler")
        files = [ROOT / "src/gpu" / name for name in ("gpu.c", "gpu_software.c", "gpu_zevio.c")]
        files += [ROOT / "src/graphics/draw.c", ROOT / "src/graphics/font.c"]
        subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "include"), str(source), *map(str, files),
                        "-o", str(exe)], check=True, capture_output=True)
        data = subprocess.check_output([str(exe)])
    return struct.unpack("=256H", data)


def run_single_test(test, emulator, bios, out):
    out.mkdir(parents=True, exist_ok=True)
    result = out / "gpu-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_{test}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/{test}/homebrew.cue", out, target=f"gpu_{test}_disc_image")
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda n: struct.unpack_from("<I", ram, sy[n] - 0x10000000)[0]
    if word("gpu_status") != (0x47505532 if test == "native" else 0x47505531):
        raise ValueError("GPU operation did not complete")
    if word("video_status") != 0x56494431 or word("render_status") != 0x44524157:
        raise ValueError("video initialization/present failed")
    pixels = [0] * (320 * 240)
    if test == "native":
        raw = struct.unpack_from("<8192H", ram, sy["gpu_tiled"] - 0x10000000)
        if raw != software_fill_reference() + (0x255A,) * (8192 - 256):
            raise ValueError("software/native fill or untouched-region check failed")
        if word("gpu_completion") != 0xFD08:
            raise ValueError("GE completion status")
        for y in range(8):
            pixels[y * 320:y * 320 + 32] = [31] * 32
        stats = {n: word(n) for n in ("gpu_submit_ticks", "gpu_wait_ticks", "gpu_completion")}
    else:
        for y in range(10, 70):
            for x in range(10, 110):
                pixels[y * 320 + x] = 31
        for y in range(40, 48):
            for x in range(60, 68):
                pixels[y * 320 + x] = 0x3E0
        for y in range(20, 60):
            for x in range(20, 60):
                pixels[y * 320 + x] = 0x7C00
        stats = {n: word(n) for n in ("gpu_clear_ticks", "gpu_fill_ticks", "gpu_blit_ticks")}
    native = struct.pack("<76800H", *pixels)
    if ram[sy["video_pixels"] - 0x10000000:][:len(native)] != native:
        raise ValueError("linear framebuffer mismatch")
    if read_ppm(out / "frame.ppm") != (320, 240, to_rgb(native)):
        raise ValueError("actual scanout mismatch")
    result.write_text(json.dumps(dict(
        passed=True, backend=test, statistics=stats, timer_resolution_us=100,
        scope="command-driven emulator fill; CPU linear presentation copy" if test == "native"
        else "synchronous software; eight operations per measured batch"
    ), indent=2) + "\n")
    print(result.read_text())


def run_tiled(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "tiled-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_tiled_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/tiled/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    assert word("tiled_status") == 0x54494C31 and word("tiled_completions") == 5
    assert word("tiled_swaps") == (2 if view == "linear" else 3)
    assert bool(word("tiled_control") & 1) == (mmu == "on")
    a = [0x255A] * 16384
    b = a.copy()

    def fill(s, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                s[tiled_offset(xx, yy)] = c

    for args in [(0, 0, 32, 8, 31), (32, 0, 32, 8, 0x3E0), (0, 8, 32, 8, 0x7C00),
                 (32, 8, 32, 8, 0x7FFF), (28, 6, 12, 4, 0x3FF), (7, 3, 5, 6, 0x7C1F)]:
        fill(a, *args)
    fill(b, 0, 0, 64, 16, 0x7C00)
    fill(b, 31, 7, 2, 2, 31)
    visible_a = [a[tiled_offset(x, y)] for y in range(16) for x in range(64)]
    for y in range(16):
        for x in range(64):
            a[tiled_offset(x + 80, y)] = visible_a[y * 64 + x]
    snapshot = a.copy()
    for y in range(14):
        for x in range(60):
            a[tiled_offset(x + 84, y + 2)] = snapshot[tiled_offset(x + 80, y)]
    for name, expected in [("tiled_a", a), ("tiled_b", b)]:
        assert struct.unpack_from("<16384H", ram, sy[name] - 0x10000000) == tuple(expected), f"{name} pattern differs"
    expected_readback = tuple(a[tiled_offset(x + 80, y)] for y in range(16) for x in range(64))
    assert struct.unpack_from("<1024H", ram, sy["tiled_readback"] - 0x10000000) == expected_readback, "D1 readback differs"
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600), "CPU linear buffer must remain black"
    expected = [0] * 76800
    if view != "linear":
        source = a if view == "a" else b
        for y in range(16):
            for x in range(64):
                expected[(19 + y) * 320 + 13 + x] = source[tiled_offset(x, y)]
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *expected)))
    verify_scanout_sequence(out, [sy["tiled_a"], sy["tiled_b"], sy["video_pixels"]])
    result.write_text(json.dumps(dict(passed=True, mmu=mmu, view=view, completion_lists=5), indent=2) + "\n")
    return f"PASS tiled {mmu} {view}"


def run_frame(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "frame-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_frame_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/frame/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    assert word("gpu_frame_status") == 0x47465231 and word("gpu_frame_count") == 32
    assert word("gpu_lists") == 213
    assert bool(word("gpu_frame_control") & 1) == (mmu == "on")
    a = [0x255A] * 16384
    b = a.copy()
    wide = [0x255A] * 24576

    def fill(s, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                s[tiled_offset(xx, yy)] = c

    for args in [(0, 0, 32, 8, 31), (32, 0, 32, 8, 0x3E0), (0, 8, 32, 8, 0x7C00),
                 (32, 8, 32, 8, 0x7FFF), (28, 6, 12, 4, 0x3FF), (7, 3, 5, 6, 0x7C1F)]:
        fill(a, *args)
    fill(b, 0, 0, 64, 16, 0x3E0)
    fill(b, 31, 7, 2, 2, 31)
    fill(b, 0, 0, 16, 4, 0x7FFF)
    for surface in (a, b):
        snapshot = surface.copy()
        for y in range(4):
            for x in range(16):
                surface[tiled_offset(x + 44, y + 12)] = snapshot[tiled_offset(x, y)]
    fill(wide, 0, 0, 96, 24, 0)
    fill(wide, 64, 16, 32, 8, 0x7FE0)
    for x in range(14):
        wide[tiled_offset(x, 23)] = 0x4000 + 2 + x
    for y in range(8):
        for x in range(16):
            val = 0x4000 + y * 16 + x
            wide[tiled_offset(40 + x, 8 + y)] = val
            wide[tiled_offset(72 + x, 16 + y)] = val
    for name, count, expected in [("frame_a", 16384, a), ("frame_b", 16384, b), ("frame_wide", 24576, wide)]:
        assert struct.unpack_from(f"<{count}H", ram, sy[name] - 0x10000000) == tuple(expected), f"{name} pattern differs"
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    expected = [0] * 76800
    if view != "linear":
        source = a if view == "a" else b
        for y in range(16):
            for x in range(64):
                expected[((0 if view == "a" else 224) + y) * 320 + (0 if view == "a" else 256) + x] = source[tiled_offset(x, y)]
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *expected)))
    addresses = verify_scanout_sequence(out, [sy["frame_wide"], sy["frame_a"], sy["frame_b"], sy["video_pixels"]])
    banks = [v for v in addresses if v in (sy["frame_a"], sy["frame_b"])]
    assert len(banks) >= 32 and banks[:32] == [sy["frame_a"] if i % 2 == 0 else sy["frame_b"] for i in range(32)]
    stats = {name: word(name) for name in ("gpu_operation_ticks", "gpu_present_ticks", "gpu_total_ticks",
                                           "gpu_submission_ticks", "gpu_completion_ticks", "gpu_lists")}
    assert stats["gpu_total_ticks"] >= stats["gpu_present_ticks"] > 0
    result.write_text(json.dumps(dict(passed=True, mmu=mmu, view=view, frames=32, statistics=stats), indent=2) + "\n")
    return f"PASS frame {mmu} {view}"


def sprite_pixel(x, y):
    if x == 0 or x == 15 or y == 0 or y == 15:
        return 0x7FFF
    if x == y:
        return 0x03FF
    if x + y == 15:
        return 0x7FE0
    base = (0x001F if x < 8 else 0x03E0) if y < 8 else (0x7C00 if x < 8 else 0x7C1F)
    return base ^ (((y & 7) << 5) | (x & 7))


def run_sprite(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "sprite-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_sprite_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/sprite/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    assert word("gpu_sprite_status") == 0x47535031 and word("gpu_sprite_frames") == 32
    assert word("gpu_sprite_uploads") == 2 and word("gpu_lists") == 194
    assert bool(word("gpu_sprite_control") & 1) == (mmu == "on")
    expected_asset = tuple(sprite_pixel(x, y) for y in range(16) for x in range(16))
    assert struct.unpack_from("<256H", ram, sy["sprite_asset"] - 0x10000000) == expected_asset

    def expected_surface(final_frame):
        s = [0x255A] * 32768
        for y in range(16):
            for x in range(16):
                s[tiled_offset(104 + x, 8 + y)] = sprite_pixel(x, y)
        for yy in range(16):
            for xx in range(96):
                s[tiled_offset(xx, yy)] = draw_color(0x102060)
                s[tiled_offset(xx, 16 + yy)] = draw_color(0x105020)
        for yy in range(14, 18):
            for xx in range(96):
                s[tiled_offset(xx, yy)] = draw_color(0xC08020)
        for yy in range(32):
            for xx in (30, 31, 32, 33, 62, 63, 64, 65):
                s[tiled_offset(xx, yy)] = draw_color(0x404040)
        sx = final_frame * 2 + 8
        sy_pos = 1 + ((final_frame * 5) & 15)
        for y in range(16):
            for x in range(16):
                s[tiled_offset(sx + x, sy_pos + y)] = s[tiled_offset(104 + x, 8 + y)]
        return s

    a = expected_surface(30)
    b = expected_surface(31)
    for name, expected in [("sprite_surface_a", a), ("sprite_surface_b", b)]:
        actual = struct.unpack_from("<32768H", ram, sy[name] - 0x10000000)
        assert actual == tuple(expected), f"{name} playfield/atlas differs"
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    expected = [0] * 76800
    if view != "linear":
        source = a if view == "a" else b
        ox, oy = (0, 0) if view == "a" else (224, 208)
        for y in range(32):
            for x in range(96):
                expected[(oy + y) * 320 + ox + x] = source[tiled_offset(x, y)]
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *expected)))
    addresses = verify_scanout_sequence(out, [sy["sprite_surface_a"], sy["sprite_surface_b"], sy["video_pixels"]])
    banks = [v for v in addresses if v in (sy["sprite_surface_a"], sy["sprite_surface_b"])]
    assert len(banks) >= 32 and banks[:32] == [sy["sprite_surface_a"] if i % 2 == 0 else sy["sprite_surface_b"] for i in range(32)]
    stats = {name: word(name) for name in ("gpu_operation_ticks", "gpu_present_ticks", "gpu_total_ticks",
                                           "gpu_submission_ticks", "gpu_completion_ticks", "gpu_lists", "gpu_sprite_uploads")}
    result.write_text(json.dumps(dict(passed=True, mmu=mmu, view=view, frames=32, statistics=stats), indent=2) + "\n")
    return f"PASS sprite {mmu} {view}"


def bgr_key_pixel(x, y):
    if (x < 3 and y < 3) or (x >= 13 and y < 3):
        return 0x0000 if (x == 0 and y == 0) else 0x8000
    if (x < 3 and y >= 13) or (x >= 13 and y >= 13):
        return 0xFFFF if (x == 15 and y == 15) else 0x9ABC
    if 6 <= x <= 9 and 6 <= y <= 9:
        return 0x8000 if ((x + y) & 1) else 0x0000
    if x == 0 or x == 15 or y == 0 or y == 15:
        return 0x7FFF
    if x == y:
        return 0x03FF
    if x + y == 15:
        return 0x7FE0
    base = (0x001F if x < 8 else 0x03E0) if y < 8 else (0x7C00 if x < 8 else 0x7C1F)
    return (base ^ (((y & 7) << 5) | (x & 7))) & 0x7FFF


def abgr_pixel(x, y):
    if (x < 2 and y < 2) or (x >= 14 and y < 2) or (x < 2 and y >= 14) or (x >= 14 and y >= 14):
        return 0x0FFF
    if 6 <= x <= 9 and 6 <= y <= 9:
        if y < 8:
            return 0xF000 if x < 8 else 0xFFFF
        return 0x70FF if x < 8 else 0x3FF0
    if y < 8:
        return 0x700F if x < 8 else 0x70F0
    return 0x7F00 if x < 8 else 0xBF0F


def is_bgr_opaque(p):
    return p != 0 and not (p & 0x8000)


def blend_abgr(dst, raw):
    a = (raw >> 12) & 15
    if not a:
        return dst
    al = a + 1
    r4 = raw & 15
    g4 = (raw >> 4) & 15
    b4 = (raw >> 8) & 15
    r5 = (r4 << 1) | (r4 >> 3)
    g5 = (g4 << 1) | (g4 >> 3)
    b5 = (b4 << 1) | (b4 >> 3)
    if al == 16:
        return (b5 << 10) | (g5 << 5) | r5
    dst_r = dst & 31
    dst_g = (dst >> 5) & 31
    dst_b = (dst >> 10) & 31
    out_r = (r5 * al + dst_r * (16 - al)) // 16
    out_g = (g5 * al + dst_g * (16 - al)) // 16
    out_b = (b5 * al + dst_b * (16 - al)) // 16
    return (out_b << 10) | (out_g << 5) | out_r


def run_transparent(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "transparent-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_transparent_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/transparent/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    assert word("gpu_transparent_status") == 0x47545331 and word("gpu_transparent_frames") == 32
    assert word("gpu_transparent_uploads") == 2 and word("gpu_lists") == 226
    assert bool(word("gpu_transparent_control") & 1) == (mmu == "on")
    expected_asset = tuple(bgr_key_pixel(x, y) for y in range(16) for x in range(16))
    assert struct.unpack_from("<256H", ram, sy["transparent_asset"] - 0x10000000) == expected_asset

    def expected_surface(final_frame):
        s = [0x255A] * 32768

        def draw_sprite(sx, sy_src, w, h, dx, dy, cx, cy, cw, ch):
            l, t, r, b_edge = max(dx, cx), max(dy, cy), min(dx + w, cx + cw), min(dy + h, cy + ch)
            for yy in range(t, b_edge):
                for xx in range(l, r):
                    p = s[tiled_offset(sx + (xx - dx), sy_src + (yy - dy))]
                    if is_bgr_opaque(p):
                        s[tiled_offset(xx, yy)] = p

        for y in range(16):
            for x in range(16):
                s[tiled_offset(104 + x, 8 + y)] = bgr_key_pixel(x, y)
        for yy in range(16):
            for xx in range(96):
                s[tiled_offset(xx, yy)] = draw_color(0x102060)
                s[tiled_offset(xx, 16 + yy)] = draw_color(0x105020)
        for yy in range(14, 18):
            for xx in range(96):
                s[tiled_offset(xx, yy)] = draw_color(0xC08020)
        for yy in range(32):
            for xx in (30, 31, 32, 33, 62, 63, 64, 65):
                s[tiled_offset(xx, yy)] = draw_color(0x404040)
        sx = final_frame * 2 + 8
        sy_pos = 1 + ((final_frame * 5) & 15)
        draw_sprite(104, 8, 16, 16, sx, sy_pos, 0, 0, 128, 32)
        draw_sprite(104, 8, 16, 16, -4, 8, 2, 4, 20, 24)
        return s

    a = expected_surface(30)
    b = expected_surface(31)
    for name, expected in [("transparent_surface_a", a), ("transparent_surface_b", b)]:
        actual = struct.unpack_from("<32768H", ram, sy[name] - 0x10000000)
        assert actual == tuple(expected), f"{name} differs"
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    expected = [0] * 76800
    if view != "linear":
        source = a if view == "a" else b
        ox, oy = (0, 0) if view == "a" else (224, 208)
        for y in range(32):
            for x in range(96):
                expected[(oy + y) * 320 + ox + x] = source[tiled_offset(x, y)]
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *expected)))
    addresses = verify_scanout_sequence(out, [sy["transparent_surface_a"], sy["transparent_surface_b"], sy["video_pixels"]])
    banks = [v for v in addresses if v in (sy["transparent_surface_a"], sy["transparent_surface_b"])]
    assert len(banks) >= 32 and banks[:32] == [sy["transparent_surface_a"] if i % 2 == 0 else sy["transparent_surface_b"] for i in range(32)]
    stats = {name: word(name) for name in ("gpu_operation_ticks", "gpu_present_ticks", "gpu_total_ticks",
                                           "gpu_submission_ticks", "gpu_completion_ticks", "gpu_lists", "gpu_transparent_uploads")}
    result.write_text(json.dumps(dict(passed=True, mmu=mmu, view=view, frames=32, statistics=stats), indent=2) + "\n")
    return f"PASS transparent {mmu} {view}"


def blend_px(dst, c, blend, blend_k):
    al = (c >> 16) & 31
    if al == 0 or al > 16:
        al = 16
    on = (blend & 3) == 2
    if al == 16 and (not on or blend_k == 0xFFFFFF):
        return c & 0x7FFF
    r = 0
    for k in range(3):
        f = al * 255 * 16
        if on:
            f = al * ((blend_k >> (8 * k)) & 255) * 16
        a = (dst >> (5 * k)) & 31
        b = (c >> (5 * k)) & 31
        r |= (((b * f + a * (16 * 255 * 16 - f)) // (16 * 255 * 16)) & 31) << (5 * k)
    return r


def run_alpha(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "alpha-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_alpha_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/alpha/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    assert word("gpu_alpha_status") == 0x47414C31 and word("gpu_alpha_frames") == 32
    assert word("gpu_alpha_uploads") == 4 and word("gpu_lists") == 608
    assert bool(word("gpu_alpha_control") & 1) == (mmu == "on")
    expected_argb = tuple((0x0000 if y < 4 else (0x7FFF if y < 8 else (0x3FFF if y < 12 else 0xFFFF))) for y in range(16) for _ in range(16))
    expected_bgr = tuple((0x8000 if y < 4 else 0x7FFF) for y in range(16) for _ in range(16))
    assert struct.unpack_from("<256H", ram, sy["alpha_argb_asset"] - 0x10000000) == expected_argb
    assert struct.unpack_from("<256H", ram, sy["alpha_bgr_asset"] - 0x10000000) == expected_bgr

    s = [0x255A] * 32768
    for y in range(16):
        for x in range(16):
            s[tiled_offset(96 + x, 8 + y)] = expected_argb[y * 16 + x]
            s[tiled_offset(112 + x, 8 + y)] = expected_bgr[y * 16 + x]
    for y in range(16):
        for x in range(48):
            s[tiled_offset(x, y)] = 0x0000
            s[tiled_offset(48 + x, y)] = 0x001F
            s[tiled_offset(x, 16 + y)] = 0x03E0
            s[tiled_offset(48 + x, 16 + y)] = 0x7C00

    def draw_sprite(sx, sy_s, w, h, dx, dy, cx, cy, cw, ch, is_4444, blend_mode, blend_k):
        l, t, r, b_edge = max(dx, cx), max(dy, cy), min(dx + w, cx + cw), min(dy + h, cy + ch)
        for yy in range(t, b_edge):
            for xx in range(l, r):
                raw = s[tiled_offset(sx + (xx - dx), sy_s + (yy - dy))]
                if is_4444:
                    a = raw >> 12
                    if not a:
                        continue
                    r_ch, g_ch, b_ch = raw & 15, (raw >> 4) & 15, (raw >> 8) & 15
                    bgr = ((r_ch << 1) | (r_ch >> 3)) | (((g_ch << 1) | (g_ch >> 3)) << 5) | (((b_ch << 1) | (b_ch >> 3)) << 10)
                    c_val = bgr | ((a + 1) << 16)
                else:
                    if not raw or (raw & 0x8000):
                        continue
                    c_val = raw | (16 << 16)
                s[tiled_offset(xx, yy)] = blend_px(s[tiled_offset(xx, yy)], c_val, blend_mode, blend_k)

    for dx, dy in [(8, 0), (56, 0), (8, 16), (56, 16)]:
        draw_sprite(96, 8, 16, 16, dx, dy, 0, 0, 96, 32, True, 0, 0)
    for dx, dy in [(28, 0), (76, 0), (28, 16), (76, 16)]:
        draw_sprite(112, 8, 16, 8, dx, dy, 0, 0, 96, 32, False, 2, 0x808080)
        draw_sprite(112, 16, 16, 4, dx, dy + 8, 0, 0, 96, 32, False, 2, 0x404040)
        draw_sprite(112, 20, 16, 4, dx, dy + 12, 0, 0, 96, 32, False, 0, 0)
    draw_sprite(96, 8, 16, 16, -4, 2, 1, 2, 6, 12, True, 0, 0)
    draw_sprite(112, 8, 16, 8, -4, 18, 1, 18, 6, 12, False, 2, 0x808080)
    draw_sprite(112, 16, 16, 4, -4, 26, 1, 18, 6, 12, False, 2, 0x404040)

    for name in ("alpha_surface_a", "alpha_surface_b"):
        actual = struct.unpack_from("<32768H", ram, sy[name] - 0x10000000)
        assert actual == tuple(s), f"{name} differs"
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    expected_scanout = [0] * 76800
    if view != "linear":
        ox, oy = (0, 0) if view == "a" else (224, 208)
        for y in range(32):
            for x in range(96):
                expected_scanout[(oy + y) * 320 + ox + x] = s[tiled_offset(x, y)]
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *expected_scanout)))
    verify_scanout_sequence(out, [sy["alpha_surface_a"], sy["alpha_surface_b"], sy["video_pixels"]])
    stats = {name: word(name) for name in ("gpu_operation_ticks", "gpu_present_ticks", "gpu_total_ticks",
                                           "gpu_submission_ticks", "gpu_completion_ticks", "gpu_lists", "gpu_alpha_uploads")}
    result.write_text(json.dumps(dict(passed=True, mmu=mmu, view=view, frames=32, statistics=stats), indent=2) + "\n")
    return f"PASS alpha {mmu} {view}"


def expected_modes_or_renderer_surface():
    s = [0x255A] * 32768
    for y in range(16):
        for x in range(16):
            s[tiled_offset(96 + x, 8 + y)] = bgr_key_pixel(x, y)
            s[tiled_offset(112 + x, 8 + y)] = abgr_pixel(x, y)
    for yy in range(16):
        for xx in range(96):
            s[tiled_offset(xx, yy)] = draw_color(0x102060)
            s[tiled_offset(xx, 16 + yy)] = draw_color(0x105020)
    for yy in range(14, 18):
        for xx in range(96):
            s[tiled_offset(xx, yy)] = draw_color(0xC08020)
    for yy in range(32):
        for xx in (30, 31, 32, 33, 62, 63, 64, 65):
            s[tiled_offset(xx, yy)] = draw_color(0x404040)
    for y in range(16):
        for x in range(16):
            s[tiled_offset(4 + x, 8 + y)] = s[tiled_offset(96 + x, 8 + y)]
    for y in range(16):
        for x in range(16):
            p = s[tiled_offset(96 + x, 8 + y)]
            if is_bgr_opaque(p):
                s[tiled_offset(26 + x, 8 + y)] = p
    for y in range(16):
        for x in range(16):
            s[tiled_offset(48 + x, 8 + y)] = blend_abgr(s[tiled_offset(48 + x, 8 + y)], s[tiled_offset(112 + x, 8 + y)])
    for y in range(16):
        for x in range(16):
            p = s[tiled_offset(96 + x, 8 + y)]
            if is_bgr_opaque(p):
                s[tiled_offset(70 + x, 8 + y)] = p
    for y in range(16):
        for x in range(16):
            s[tiled_offset(52 + x, 12 + y)] = blend_abgr(s[tiled_offset(52 + x, 12 + y)], s[tiled_offset(112 + x, 8 + y)])
    for yy in range(6, 20):
        for xx in range(24, 36):
            s[tiled_offset(xx, yy)] = blend_abgr(s[tiled_offset(xx, yy)], s[tiled_offset(112 + (xx - 20), 8 + (yy - 4))])
    return s


def run_modes_or_renderer(kind, mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / f"{kind}-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_{kind}_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/{kind}/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    expected_status = 0x474D5331 if kind == "modes" else 0x47524E31
    assert word(f"gpu_{kind}_status") == expected_status and word(f"gpu_{kind}_frames") == 32
    assert word("gpu_lists") == 356
    assert bool(word(f"gpu_{kind}_control") & 1) == (mmu == "on")
    expected = expected_modes_or_renderer_surface()
    for name in (f"{kind}_surface_a", f"{kind}_surface_b"):
        actual = struct.unpack_from("<32768H", ram, sy[name] - 0x10000000)
        assert actual == tuple(expected), f"{name} differs"
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    scanout_expected = [0] * 76800
    if view != "linear":
        ox, oy = (0, 0) if view == "a" else (224, 208)
        for y in range(32):
            for x in range(96):
                val = expected[tiled_offset(x, y)]
                scanout_expected[(oy + y) * 320 + ox + x] = 0 if (val & 0x8000) else val
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *scanout_expected)))
    verify_scanout_sequence(out, [sy[f"{kind}_surface_a"], sy[f"{kind}_surface_b"], sy["video_pixels"]])
    stats = {name: word(name) for name in ("gpu_operation_ticks", "gpu_present_ticks", "gpu_total_ticks",
                                           "gpu_submission_ticks", "gpu_completion_ticks", "gpu_lists")}
    result.write_text(json.dumps({"status": "PASS", "mmu": mmu, "view": view, "stats": stats}, indent=2) + "\n")
    return f"PASS {kind} {mmu} {view}"


def run_lifecycle(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "lifecycle-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_lifecycle_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/lifecycle/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    assert word("gpu_lifecycle_status") == 0x4C494645 and word("gpu_lifecycle_frames") == 32
    assert word("gpu_exhaustion_passed") == 1 and word("gpu_busy_passed") == 1 and word("gpu_invalidation_passed") == 1
    assert bool(word("gpu_lifecycle_control") & 1) == (mmu == "on")

    s2_bgr_a = lambda x, y: 0x7FE0 if (x in (0, 7) or y in (0, 15)) else (0x8000 if 6 <= y <= 9 else (0x001F | ((x & 7) << 5) | ((y & 15) << 10)) & 0x7FFF)
    s2_abgr_b = lambda x, y: 0x0FFF if (x in (0, 7) or y in (0, 15)) else (0x7000 | (x & 15) | ((y & 15) << 8)) & 0xFFFF
    s2_abgr_c = lambda x, y: 0xF000 if ((x < 3 or x >= 13) and (y < 3 or y >= 13)) else (0xFFFF if (5 <= x <= 10 and 5 <= y <= 10) else (0x8000 | ((x & 15) << 4) | (y & 15)) & 0xFFFF)

    s = [0x255A] * 32768
    for y in range(16):
        for x in range(8):
            s[tiled_offset(96 + x, 8 + y)] = s2_bgr_a(x, y)
            s[tiled_offset(104 + x, 8 + y)] = s2_abgr_b(x, y)
        for x in range(16):
            s[tiled_offset(112 + x, 8 + y)] = s2_abgr_c(x, y)
    for yy in range(32):
        for xx in range(96):
            s[tiled_offset(xx, yy)] = draw_color(0x602010 if yy < 16 else 0x203050)
    for yy in range(12, 16):
        for xx in range(96):
            s[tiled_offset(xx, yy)] = draw_color(0x904010)
    for y in range(16):
        for x in range(8):
            s[tiled_offset(8 + x, 8 + y)] = s[tiled_offset(96 + x, 8 + y)]
            p = s[tiled_offset(96 + x, 8 + y)]
            if is_bgr_opaque(p):
                s[tiled_offset(24 + x, 8 + y)] = p
            s[tiled_offset(44 + x, 8 + y)] = blend_abgr(s[tiled_offset(44 + x, 8 + y)], s[tiled_offset(104 + x, 8 + y)])
        for x in range(16):
            s[tiled_offset(64 + x, 8 + y)] = blend_abgr(s[tiled_offset(64 + x, 8 + y)], s[tiled_offset(112 + x, 8 + y)])
    for yy in range(6, 22):
        for xx in range(36, 48):
            s[tiled_offset(xx, yy)] = blend_abgr(s[tiled_offset(xx, yy)], s[tiled_offset(112 + (xx - 32), 8 + (yy - 6))])

    for name in ("lifecycle_surface_a", "lifecycle_surface_b"):
        assert struct.unpack_from("<32768H", ram, sy[name] - 0x10000000) == tuple(s), f"{name} differs"
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    scanout_expected = [0] * 76800
    if view != "linear":
        ox, oy = (0, 0) if view == "a" else (224, 208)
        for y in range(32):
            for x in range(96):
                val = s[tiled_offset(x, y)]
                scanout_expected[(oy + y) * 320 + ox + x] = 0 if (val & 0x8000) else val
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *scanout_expected)))
    verify_scanout_sequence(out, [sy["lifecycle_surface_a"], sy["lifecycle_surface_b"], sy["video_pixels"]])
    stats = {name: word(name) for name in ("gpu_operation_ticks", "gpu_present_ticks", "gpu_total_ticks",
                                           "gpu_submission_ticks", "gpu_completion_ticks", "gpu_lists")}
    result.write_text(json.dumps({"status": "PASS", "mmu": mmu, "view": view, "stats": stats}, indent=2) + "\n")
    return f"PASS lifecycle {mmu} {view}"


def run_sdl(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "sdl-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_sdl_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/sdl/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    assert word("gpu_sdl_status") == 0x53444C32 and word("gpu_sdl_frames") == 32
    assert word("gpu_sdl_scaling_rejected") == 1 and word("gpu_sdl_live_rejected") == 1
    assert bool(word("gpu_sdl_control") & 1) == (mmu == "on")

    def make_rgb555_bgr(x, y):
        p = 0x7FE0 if (x in (0, 15) or y in (0, 15)) else (0x0000 if (6 <= x <= 9 and 6 <= y <= 9) else (((x & 15) << 11) | ((y & 15) << 6) | (((x + y) & 15) << 1)))
        return ((p & 31) << 10) | (p & 0x03E0) | ((p >> 10) & 31)

    def make_argb8888_abgr4444(x, y):
        if (x < 2 or x >= 14) and (y < 2 or y >= 14):
            p = 0x00FF0000
        elif 6 <= x <= 9 and 6 <= y <= 9:
            p = (0xFF000000 if x < 8 else 0xFFFFFFFF) if y < 8 else (0x8000FFFF if x < 8 else 0x40FFFF00)
        else:
            p = (0x80FF0000 if x < 8 else 0x8000FF00) if y < 8 else (0x800000FF if x < 8 else 0xC0FF00FF)
        return (((p >> 28) & 15) << 12) | (((p >> 4) & 15) << 8) | (((p >> 12) & 15) << 4) | ((p >> 20) & 15)

    s = [0x255A] * 32768
    for y in range(16):
        for x in range(16):
            s[tiled_offset(96 + x, 8 + y)] = make_rgb555_bgr(x, y)
            s[tiled_offset(112 + x, 8 + y)] = make_argb8888_abgr4444(x, y)
    for yy in range(16):
        for xx in range(96):
            s[tiled_offset(xx, yy)] = draw_color(0x102060)
            s[tiled_offset(xx, 16 + yy)] = draw_color(0x105020)
    for yy in range(14, 18):
        for xx in range(96):
            s[tiled_offset(xx, yy)] = draw_color(0xC08020)
    for yy in range(32):
        for xx in (30, 31, 32, 33, 62, 63, 64, 65):
            s[tiled_offset(xx, yy)] = draw_color(0x404040)
    for y in range(16):
        for x in range(16):
            s[tiled_offset(4 + x, 8 + y)] = s[tiled_offset(96 + x, 8 + y)]
            s[tiled_offset(36 + x, 8 + y)] = blend_abgr(s[tiled_offset(36 + x, 8 + y)], s[tiled_offset(112 + x, 8 + y)])
    for y in range(16):
        for x in range(16):
            s[tiled_offset(42 + x, 12 + y)] = blend_abgr(s[tiled_offset(42 + x, 12 + y)], s[tiled_offset(112 + x, 8 + y)])
    for yy in range(6, 20):
        for xx in range(64, 76):
            s[tiled_offset(xx, yy)] = blend_abgr(s[tiled_offset(xx, yy)], s[tiled_offset(112 + (xx - 60), 8 + (yy - 4))])

    for name in ("sdl_surface_a", "sdl_surface_b"):
        assert struct.unpack_from("<32768H", ram, sy[name] - 0x10000000) == tuple(s), f"{name} differs"
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    scanout_expected = [0] * 76800
    if view != "linear":
        ox, oy = (0, 0) if view == "a" else (224, 208)
        for y in range(32):
            for x in range(96):
                val = s[tiled_offset(x, y)]
                scanout_expected[(oy + y) * 320 + ox + x] = 0 if (val & 0x8000) else val
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *scanout_expected)))
    verify_scanout_sequence(out, [sy["sdl_surface_a"], sy["sdl_surface_b"], sy["video_pixels"]])
    stats = {name: word(name) for name in ("gpu_operation_ticks", "gpu_present_ticks", "gpu_total_ticks",
                                           "gpu_submission_ticks", "gpu_completion_ticks", "gpu_lists")}
    result.write_text(json.dumps({"status": "PASS", "mmu": mmu, "view": view, "stats": stats}, indent=2) + "\n")
    return f"PASS sdl {mmu} {view}"


def run_sdl2(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "sdl2-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_sdl2_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/sdl2/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    assert word("gpu_sdl2_status") == 0x600D5D12 and word("gpu_sdl2_frames") >= 32
    assert bool(word("gpu_sdl2_control") & 1) == (mmu == "on")
    assert word("gpu_lists") > 0 and word("gpu_heap_used") > 0 and word("gpu_alloc_count") > 0
    text = (out / "run.log").read_text(errors="replace")
    addresses = [int(v, 16) for v in re.findall(r"layer 0 address ([0-9A-Fa-f]{8})", text)]
    w, h, pixels = read_ppm(out / "frame.ppm")
    assert (w, h) == (320, 240)
    if view == "linear":
        assert addresses[-1] == sy["video_pixels"] and all(b == 0 for b in pixels)
    else:
        colors = {}
        for i in range(0, len(pixels), 3):
            c = (pixels[i], pixels[i + 1], pixels[i + 2])
            colors[c] = colors.get(c, 0) + 1
        assert colors.get((165, 165, 165), 0) > 70000 and colors.get((255, 255, 0), 0) > 1000 and colors.get((0, 0, 0), 0) > 500
    stats = {
        "frames": word("gpu_sdl2_frames"),
        "lists": word("gpu_lists"),
        "heap_used": word("gpu_heap_used"),
        "alloc_count": word("gpu_alloc_count"),
    }
    result.write_text(json.dumps({"status": "PASS", "mmu": mmu, "view": view, "stats": stats}, indent=2) + "\n")
    return f"PASS sdl2 {mmu} {view}"


def draw_tri(fb, v0, v1, v2, color, scissor=None, z_buf=None, z0=None):
    x0, y0 = v0
    x1, y1 = v1
    x2, y2 = v2
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
    sc_x0, sc_y0, sc_x1, sc_y1 = scissor if scissor else (0, 0, 319, 239)
    for y in range(sc_y0, sc_y1 + 1):
        for x in range(sc_x0, sc_x1 + 1):
            px, py = x + 0.5, y + 0.5
            w0 = (x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)
            w1 = (x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)
            w2 = (x0 - px) * (y1 - py) - (y0 - py) * (x1 - px)
            inside = (w0 <= 0 and w1 <= 0 and w2 <= 0) if area < 0 else (w0 >= 0 and w1 >= 0 and w2 >= 0)
            if inside:
                idx = y * 320 + x
                if z_buf is not None:
                    if z0 < z_buf[idx]:
                        z_buf[idx] = z0
                        fb[idx] = color
                else:
                    fb[idx] = color


def run_triangle_or_depth(kind, mmu, view, emulator, bios, base):
    sub = "triangle" if kind == "triangle" else "triangle_depth"
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / f"{kind}-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_{sub}_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/{sub}/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    expected_frames = 33 if view == "a" else 32
    assert word("gpu_triangle_status") == (0x54524931 if kind == "triangle" else 0x5A444550)
    assert word("gpu_triangle_frames") == expected_frames
    assert bool(word("gpu_triangle_control") & 1) == (mmu == "on")

    expected_playfield = [0x1063] * (320 * 240)
    if kind == "triangle":
        draw_tri(expected_playfield, (160, 40), (80, 180), (240, 180), 0x001F)
        draw_tri(expected_playfield, (30, 30), (90, 30), (60, 90), 0x03E0)
        draw_tri(expected_playfield, (250, 30), (180, 140), (320, 140), 0x7C00, scissor=(200, 20, 299, 119))
    else:
        z_buf = [1.0e30] * (320 * 240)
        draw_tri(expected_playfield, (80, 40), (20, 160), (140, 160), 0x001F, z_buf=z_buf, z0=2.0)
        draw_tri(expected_playfield, (120, 70), (60, 190), (180, 190), 0x03E0, z_buf=z_buf, z0=1.0)
        draw_tri(expected_playfield, (230, 40), (170, 160), (290, 160), 0x7C00)
        draw_tri(expected_playfield, (270, 70), (210, 190), (330, 190), 0x03FF)

    for name in ("triangle_surface_a", "triangle_surface_b"):
        s_base = sy[name] - 0x10000000
        for y in range(240):
            for x in range(320):
                assert struct.unpack_from("<H", ram, s_base + tiled_offset(x, y) * 2)[0] == expected_playfield[y * 320 + x]
            for x in range(320, 1024):
                assert struct.unpack_from("<H", ram, s_base + tiled_offset(x, y) * 2)[0] == 0x255A
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    expected_scanout = [0] * 76800 if view == "linear" else expected_playfield
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *expected_scanout)))
    if view == "linear":
        verify_scanout_sequence(out, [sy["triangle_surface_a"], sy["triangle_surface_b"], sy["video_pixels"]])
    result.write_text(json.dumps(dict(passed=True, mmu=mmu, view=view, frames=expected_frames), indent=2) + "\n")
    return f"PASS {kind} {mmu} {view}"


def bilinear_sample(u_norm, v_norm, tex, w=32, h=32):
    u_prime, v_prime = u_norm * w - 0.5, v_norm * h - 0.5
    u0, v0 = math.floor(u_prime), math.floor(v_prime)
    fu, fv = u_prime - u0, v_prime - v0
    t00 = tex[(v0 & (h - 1)) * w + (u0 & (w - 1))]
    t10 = tex[(v0 & (h - 1)) * w + ((u0 + 1) & (w - 1))]
    t01 = tex[((v0 + 1) & (h - 1)) * w + (u0 & (w - 1))]
    t11 = tex[((v0 + 1) & (h - 1)) * w + ((u0 + 1) & (w - 1))]
    if t00 <= 0 or t10 <= 0 or t01 <= 0 or t11 <= 0:
        return [t00, t10, t01, t11][int(fu >= 0.5) + int(fv >= 0.5) * 2]
    w00, w10, w01, w11 = (1.0 - fu) * (1.0 - fv), fu * (1.0 - fv), (1.0 - fu) * fv, fu * fv
    res = 0
    for shift in (0, 5, 10):
        val = int(w00 * ((t00 >> shift) & 0x1F) + w10 * ((t10 >> shift) & 0x1F) +
                  w01 * ((t01 >> shift) & 0x1F) + w11 * ((t11 >> shift) & 0x1F) + 0.5)
        res |= (val & 0x1F) << shift
    return res


def run_triangle_tex(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "triangle-tex-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_triangle_tex_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/triangle_tex/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    expected_frames = 33 if view == "a" else 32
    assert word("gpu_triangle_status") == 0x54455831 and word("gpu_triangle_frames") == expected_frames
    assert bool(word("gpu_triangle_control") & 1) == (mmu == "on")
    tex = [(0x001F if u < 16 else 0x03E0) if v < 16 else (0x7C00 if u < 16 else 0x03FF) for v in range(32) for u in range(32)]
    for name in ("triangle_surface_a", "triangle_surface_b"):
        s_base = sy[name] - 0x10000000
        for v in range(32):
            for u in range(32):
                assert struct.unpack_from("<H", ram, s_base + tiled_offset(u, 240 + v) * 2)[0] == tex[v * 32 + u]
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    expected_scanout = [0] * 76800
    if view != "linear":
        target_base = sy["triangle_surface_a" if view == "a" else "triangle_surface_b"] - 0x10000000
        expected_scanout = [struct.unpack_from("<H", ram, target_base + tiled_offset(x, y) * 2)[0] for y in range(240) for x in range(320)]
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *expected_scanout)))
    if view == "linear":
        verify_scanout_sequence(out, [sy["triangle_surface_a"], sy["triangle_surface_b"], sy["video_pixels"]])
    result.write_text(json.dumps(dict(passed=True, mmu=mmu, view=view, frames=expected_frames), indent=2) + "\n")
    return f"PASS triangle-tex {mmu} {view}"


def run_cube(mmu, view, emulator, bios, base):
    out = base / f"{mmu}-{view}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "cube-result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_gpu_cube_{mmu}_{view}.elf"
    run_emulator_smoke(emulator, bios, elf, ROOT / f"build/gpu/cube/{mmu}/{view}/homebrew.cue", out, trace_video=True)
    sy = load_symbols(elf)
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    expected_frames = 33 if view == "a" else 32
    assert word("gpu_cube_status") == 0x43554231 and word("gpu_cube_frames") == expected_frames
    assert bool(word("gpu_cube_control") & 1) == (mmu == "on")
    for name in ("cube_surface_a", "cube_surface_b"):
        s_base = sy[name] - 0x10000000
        non_bg = sum(1 for y in range(240) for x in range(320) if struct.unpack_from("<H", ram, s_base + tiled_offset(x, y) * 2)[0] != 0x1063)
        assert non_bg > 2000, f"{name} missing rendered cube pixels"
        for y in range(240):
            for x in range(320, 1024):
                assert struct.unpack_from("<H", ram, s_base + tiled_offset(x, y) * 2)[0] == 0x255A
    assert ram[sy["video_pixels"] - 0x10000000:][:153600] == bytes(153600)
    expected_scanout = [0] * 76800
    if view != "linear":
        target_base = sy["cube_surface_a" if view == "a" else "cube_surface_b"] - 0x10000000
        expected_scanout = [struct.unpack_from("<H", ram, target_base + tiled_offset(x, y) * 2)[0] for y in range(240) for x in range(320)]
    assert read_ppm(out / "frame.ppm") == (320, 240, to_rgb(struct.pack("<76800H", *expected_scanout)))
    if view == "linear":
        verify_scanout_sequence(out, [sy["cube_surface_a"], sy["cube_surface_b"], sy["video_pixels"]])
    result.write_text(json.dumps(dict(passed=True, mmu=mmu, view=view, frames=expected_frames), indent=2) + "\n")
    return f"PASS cube {mmu} {view}"


DISPATCH = {
    "tiled": run_tiled,
    "frame": run_frame,
    "sprite": run_sprite,
    "transparent": run_transparent,
    "alpha": run_alpha,
    "modes": lambda m, v, e, b, o: run_modes_or_renderer("modes", m, v, e, b, o),
    "renderer": lambda m, v, e, b, o: run_modes_or_renderer("renderer", m, v, e, b, o),
    "lifecycle": run_lifecycle,
    "sdl": run_sdl,
    "sdl2": run_sdl2,
    "triangle": lambda m, v, e, b, o: run_triangle_or_depth("triangle", m, v, e, b, o),
    "triangle-depth": lambda m, v, e, b, o: run_triangle_or_depth("triangle-depth", m, v, e, b, o),
    "triangle-tex": run_triangle_tex,
    "cube": run_cube,
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--emulator", type=Path, default=ROOT / "build/emulator/flashem")
    p.add_argument("--bios", type=Path, default=ROOT / "dumps/A29L160ATV@TSOP48.BIN")
    p.add_argument("--test", choices=ALL_TESTS, required=True)
    p.add_argument("--out", type=Path, default=None)
    p.add_argument("--mmu", choices=("off", "on", "both"), default="both")
    p.add_argument("--view", choices=("a", "b", "linear", "all"), default="all")
    p.add_argument("--parallel", type=int, default=6)
    args = p.parse_args()
    out = args.out or (ROOT / f"build/gpu-{args.test}-acceptance")
    if args.test in ("software", "mmu", "native"):
        run_single_test(args.test, args.emulator, args.bios, out)
        return
    mmus = ("off", "on") if args.mmu == "both" else (args.mmu,)
    views = ("a", "b", "linear") if args.view == "all" else (args.view,)
    variants = [(m, v) for m in mmus for v in views]
    kind = args.test.replace("-", "_")
    targets = [f"gpu_{kind}_{m}_{v}_disc_image" for m, v in variants]
    subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", *targets],
                   check=True, stdout=subprocess.DEVNULL)
    fn = DISPATCH[args.test]
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(len(variants), args.parallel)) as pool:
        futures = [pool.submit(fn, m, v, args.emulator, args.bios, out) for m, v in variants]
        for f in futures:
            print(f.result(), flush=True)


if __name__ == "__main__":
    main()
