"""Verify SDL2-compatible rendering layer (SDL_* over vflash_renderer_t):
pixel format conversions, scaling rejection, clip handling, clear semantics,
and dual-surface coherency."""
import argparse, concurrent.futures, json, re, struct, subprocess, sys
from pathlib import Path
from host_renderer import read_ppm, to_rgb

ROOT = Path(__file__).resolve().parents[1]

def draw_color(rgb):
    r = (rgb >> 19) & 31
    g = (rgb >> 11) & 31
    b = (rgb >> 3) & 31
    return (b << 10) | (g << 5) | r

def make_rgb555(x, y):
    if x == 0 or x == 15 or y == 0 or y == 15:
        return 0x7FE0
    if 6 <= x <= 9 and 6 <= y <= 9:
        return 0x0000
    r5 = (x & 15) << 1
    g5 = (y & 15) << 1
    b5 = ((x + y) & 15) << 1
    return (r5 << 10) | (g5 << 5) | b5

def make_argb8888(x, y):
    if (x < 2 and y < 2) or (x >= 14 and y < 2) or (x < 2 and y >= 14) or (x >= 14 and y >= 14):
        return 0x00FF0000
    if 6 <= x <= 9 and 6 <= y <= 9:
        if y < 8:
            return 0xFF000000 if x < 8 else 0xFFFFFFFF
        return 0x8000FFFF if x < 8 else 0x40FFFF00
    if y < 8:
        return 0x80FF0000 if x < 8 else 0x8000FF00
    return 0x800000FF if x < 8 else 0xC0FF00FF

def rgb555_to_bgr555(p):
    r5 = (p >> 10) & 31
    g5 = (p >> 5) & 31
    b5 = p & 31
    return (b5 << 10) | (g5 << 5) | r5

def argb8888_to_abgr4444(p):
    a = (p >> 24) & 0xFF
    r = (p >> 16) & 0xFF
    g = (p >> 8) & 0xFF
    b = p & 0xFF
    a4 = a >> 4
    r4 = r >> 4
    g4 = g >> 4
    b4 = b >> 4
    return (a4 << 12) | (b4 << 8) | (g4 << 4) | r4

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

def run(mmu, view, emulator, bios, base):
    out = base / f'{mmu}-{view}'
    out.mkdir(parents=True, exist_ok=True)
    result = out / 'sdl-result.json'
    result.unlink(missing_ok=True)
    elf = ROOT / f'build/vflash_gpu_sdl_{mmu}_{view}.elf'
    subprocess.run([
        sys.executable, str(ROOT / 'tools/emulator_smoke.py'),
        '--emulator', str(emulator), '--bios', str(bios),
        '--elf', str(elf),
        '--disc', str(ROOT / f'build/gpu/sdl/{mmu}/{view}/homebrew.cue'),
        '--out', str(out), '--trace-video'
    ], check=True, stdout=subprocess.DEVNULL, timeout=150)
    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ['arm-none-eabi-nm', '--defined-only', str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / 'ram.bin').read_bytes()
    word = lambda name: struct.unpack_from('<I', ram, sy[name] - 0x10000000)[0]
    assert word('gpu_sdl_status') == 0x53444C32 and word('gpu_sdl_frames') == 32
    assert word('gpu_sdl_scaling_rejected') == 1, 'scaling rejection check failed'
    assert word('gpu_sdl_live_rejected') == 1, 'live upload rejection check failed'
    assert bool(word('gpu_sdl_control') & 1) == (mmu == 'on')

    offset = lambda x, y: (y // 8) * 8192 + (x // 32) * 256 + (y % 8) * 32 + x % 32

    def expected_surface():
        s = [0x255A] * 32768
        def fill(x, y, w, h, c):
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    s[offset(xx, yy)] = c

        # Atlas:
        # - Texture 1 (RGB555 -> BGR555) at (96, 8)
        # - Texture 2 (ARGB8888 -> ABGR4444) at (112, 8)
        for y in range(16):
            for x in range(16):
                s[offset(96 + x, 8 + y)] = rgb555_to_bgr555(make_rgb555(x, y))
                s[offset(112 + x, 8 + y)] = argb8888_to_abgr4444(make_argb8888(x, y))

        # Clear playfield + fills
        fill(0, 0, 96, 32, draw_color(0x102060))
        fill(0, 16, 96, 16, draw_color(0x105020))
        fill(0, 14, 96, 4, draw_color(0xC08020))
        fill(30, 0, 4, 32, draw_color(0x404040))
        fill(62, 0, 4, 32, draw_color(0x404040))

        # Draw 1: Opaque BGR555 texture copy to (4, 8)
        for y in range(16):
            for x in range(16):
                s[offset(4 + x, 8 + y)] = s[offset(96 + x, 8 + y)]

        # Draw 2: Translucent ABGR4444 texture to (36, 8)
        for y in range(16):
            for x in range(16):
                p = s[offset(112 + x, 8 + y)]
                s[offset(36 + x, 8 + y)] = blend_abgr(s[offset(36 + x, 8 + y)], p)

        # Draw 3: Overlapping translucent texture to (42, 12)
        for y in range(16):
            for x in range(16):
                p = s[offset(112 + x, 8 + y)]
                s[offset(42 + x, 12 + y)] = blend_abgr(s[offset(42 + x, 12 + y)], p)

        # Draw 4: Clipped ARGB8888 texture at (60, 4) with scissor (64, 6, 20, 16)
        # Intersect sprite (60, 4, 16, 16) with clip (64, 6, 20, 16) -> x: 64..76, y: 6..20
        for yy in range(6, 20):
            for xx in range(64, 76):
                u = 112 + (xx - 60)
                v = 8 + (yy - 4)
                p = s[offset(u, v)]
                s[offset(xx, yy)] = blend_abgr(s[offset(xx, yy)], p)

        return s

    expected = expected_surface()
    for name in ('sdl_surface_a', 'sdl_surface_b'):
        actual = struct.unpack_from('<32768H', ram, sy[name] - 0x10000000)
        assert actual == tuple(expected), f'{name} playfield, atlas or untouched padding differs'
        for y in range(32):
            for x in range(96, 128):
                if 96 <= x < 112 and 8 <= y < 24:
                    exp_atlas = rgb555_to_bgr555(make_rgb555(x - 96, y - 8))
                elif 112 <= x < 128 and 8 <= y < 24:
                    exp_atlas = argb8888_to_abgr4444(make_argb8888(x - 112, y - 8))
                else:
                    exp_atlas = 0x255A
                assert actual[offset(x, y)] == exp_atlas, f'{name} atlas corrupted at ({x},{y})'

    assert ram[sy['video_pixels'] - 0x10000000:][:153600] == bytes(153600), 'CPU linear buffer must remain black'
    scanout_expected = [0] * 76800
    if view != 'linear':
        source = expected
        ox, oy = (0, 0) if view == 'a' else (224, 208)
        for y in range(32):
            for x in range(96):
                val = source[offset(x, y)]
                if val & 0x8000:
                    val = 0
                scanout_expected[(oy + y) * 320 + ox + x] = val
    assert read_ppm(out / 'frame.ppm') == (320, 240, to_rgb(struct.pack('<76800H', *scanout_expected))), 'direct viewport scanout/placement/recovery mismatch'
    text = (out / 'run.log').read_text(errors='replace')
    addresses = [int(v, 16) for v in re.findall(r'layer 0 address ([0-9A-Fa-f]{8})', text)]
    sequence = [sy['sdl_surface_a'], sy['sdl_surface_b'], sy['video_pixels']]
    for address in addresses:
        if sequence and address == sequence[0]: sequence.pop(0)
    assert not sequence, 'scanout did not visit A, B, then linear recovery'
    banks = [v for v in addresses if v in (sy['sdl_surface_a'], sy['sdl_surface_b'])]
    assert len(banks) >= 32
    stats = {name: word(name) for name in ('gpu_operation_ticks', 'gpu_present_ticks', 'gpu_total_ticks', 'gpu_submission_ticks', 'gpu_completion_ticks', 'gpu_lists')}
    result.write_text(json.dumps({'status': 'PASS', 'mmu': mmu, 'view': view, 'control': hex(word('gpu_sdl_control')), 'stats': stats}, indent=2))
    return f'PASS gpu_sdl_{mmu}_{view}: 32 frames, {stats["gpu_lists"]} lists, VRAM flips={len(banks)}, {stats["gpu_operation_ticks"]} render ticks, {stats["gpu_present_ticks"]} present ticks'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', default=str(ROOT / 'build/emulator/flashem'))
    p.add_argument('--bios', default=str(ROOT / 'dumps/A29L160ATV@TSOP48.BIN'))
    p.add_argument('--out', default=str(ROOT / 'build/gpu-sdl-acceptance'))
    p.add_argument('--parallel', type=int, default=6)
    p.add_argument('--mmu', choices=['off', 'on', 'both'], default='both')
    p.add_argument('--view', choices=['a', 'b', 'linear', 'all'], default='all')
    args = p.parse_args()
    mmus = ['off', 'on'] if args.mmu == 'both' else [args.mmu]
    views = ['a', 'b', 'linear'] if args.view == 'all' else [args.view]
    variants = [(m, v) for m in mmus for v in views]
    base = Path(args.out)
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(len(variants), args.parallel)) as ex:
        futs = {ex.submit(run, m, v, Path(args.emulator), Path(args.bios), base): f'{m}-{v}' for m, v in variants}
        for f in concurrent.futures.as_completed(futs):
            print(f.result())

if __name__ == '__main__':
    main()

