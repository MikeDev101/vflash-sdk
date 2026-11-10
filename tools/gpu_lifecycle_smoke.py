"""Verify 2D renderer lifecycle and atlas management:
texture invalidation, atlas reset, allocation exhaustion, frontbuffer upload protection,
and dual-surface coherency across scene changes."""
import argparse, concurrent.futures, json, re, struct, subprocess, sys
from pathlib import Path
from host_renderer import read_ppm, to_rgb

ROOT = Path(__file__).resolve().parents[1]

def draw_color(rgb):
    r = (rgb >> 19) & 31
    g = (rgb >> 11) & 31
    b = (rgb >> 3) & 31
    return (b << 10) | (g << 5) | r

def s1_bgr_pixel(x, y):
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

def s1_abgr_pixel(x, y):
    if (x < 2 and y < 2) or (x >= 14 and y < 2) or (x < 2 and y >= 14) or (x >= 14 and y >= 14):
        return 0x0FFF
    if 6 <= x <= 9 and 6 <= y <= 9:
        if y < 8:
            return 0xF000 if x < 8 else 0xFFFF
        return 0x70FF if x < 8 else 0x3FF0
    if y < 8:
        return 0x700F if x < 8 else 0x70F0
    return 0x7F00 if x < 8 else 0xBF0F

def s2_bgr_a_pixel(x, y):
    if x == 0 or x == 7 or y == 0 or y == 15:
        return 0x7FE0
    if 6 <= y <= 9:
        return 0x8000
    return (0x001F | ((x & 7) << 5) | ((y & 15) << 10)) & 0x7FFF

def s2_abgr_b_pixel(x, y):
    if x == 0 or x == 7 or y == 0 or y == 15:
        return 0x0FFF
    return (0x7000 | (x & 15) | ((y & 15) << 8)) & 0xFFFF

def s2_abgr_c_pixel(x, y):
    if (x < 3 and y < 3) or (x >= 13 and y < 3) or (x < 3 and y >= 13) or (x >= 13 and y >= 13):
        return 0xF000
    if 5 <= x <= 10 and 5 <= y <= 10:
        return 0xFFFF
    return (0x8000 | ((x & 15) << 4) | (y & 15)) & 0xFFFF

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

def run(mmu, view, emulator, bios, base):
    out = base / f'{mmu}-{view}'
    out.mkdir(parents=True, exist_ok=True)
    result = out / 'lifecycle-result.json'
    result.unlink(missing_ok=True)
    elf = ROOT / f'build/vflash_gpu_lifecycle_{mmu}_{view}.elf'
    subprocess.run([
        sys.executable, str(ROOT / 'tools/emulator_smoke.py'),
        '--emulator', str(emulator), '--bios', str(bios),
        '--elf', str(elf),
        '--disc', str(ROOT / f'build/gpu/lifecycle/{mmu}/{view}/homebrew.cue'),
        '--out', str(out), '--trace-video'
    ], check=True, stdout=subprocess.DEVNULL, timeout=150)
    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ['arm-none-eabi-nm', '--defined-only', str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / 'ram.bin').read_bytes()
    word = lambda name: struct.unpack_from('<I', ram, sy[name] - 0x10000000)[0]
    assert word('gpu_lifecycle_status') == 0x4C494645 and word('gpu_lifecycle_frames') == 32
    assert word('gpu_exhaustion_passed') == 1, 'allocation exhaustion check failed'
    assert word('gpu_busy_passed') == 1, 'frontbuffer upload protection check failed'
    assert word('gpu_invalidation_passed') == 1, 'texture invalidation / stale handle check failed'
    assert bool(word('gpu_lifecycle_control') & 1) == (mmu == 'on')

    offset = lambda x, y: (y // 8) * 8192 + (x // 32) * 256 + (y % 8) * 32 + x % 32

    def expected_surface():
        s = [0x255A] * 32768
        def fill(x, y, w, h, c):
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    s[offset(xx, yy)] = c

        # Final state corresponds to Scene 2:
        # Atlas:
        # - Pattern A (8x16 BGR) at (96, 8)
        # - Pattern B (8x16 ABGR) at (104, 8)
        # - Asset C (16x16 ABGR) at (112, 8)
        for y in range(16):
            for x in range(8):
                s[offset(96 + x, 8 + y)] = s2_bgr_a_pixel(x, y)
                s[offset(104 + x, 8 + y)] = s2_abgr_b_pixel(x, y)
        for y in range(16):
            for x in range(16):
                s[offset(112 + x, 8 + y)] = s2_abgr_c_pixel(x, y)

        # Clear Scene 2 playfield + fills
        fill(0, 0, 96, 32, draw_color(0x602010))
        fill(0, 16, 96, 16, draw_color(0x203050))
        fill(0, 12, 96, 4, draw_color(0x904010))

        # Draw 1: Opaque copy of A at (8, 8)
        for y in range(16):
            for x in range(8):
                s[offset(8 + x, 8 + y)] = s[offset(96 + x, 8 + y)]

        # Draw 2: Keyed sprite of A at (24, 8)
        for y in range(16):
            for x in range(8):
                p = s[offset(96 + x, 8 + y)]
                if is_bgr_opaque(p):
                    s[offset(24 + x, 8 + y)] = p

        # Draw 3: Translucent sprite of B at (44, 8)
        for y in range(16):
            for x in range(8):
                p = s[offset(104 + x, 8 + y)]
                s[offset(44 + x, 8 + y)] = blend_abgr(s[offset(44 + x, 8 + y)], p)

        # Draw 4: Translucent sprite of C at (64, 8)
        for y in range(16):
            for x in range(16):
                p = s[offset(112 + x, 8 + y)]
                s[offset(64 + x, 8 + y)] = blend_abgr(s[offset(64 + x, 8 + y)], p)

        # Draw 5: Clipped draw of C at (32, 6) clipped to (36, 4, 32, 20)
        # Intersect sprite (32, 6, 16, 16) with clip (36, 4, 32, 20) -> x: 36..48, y: 6..22
        for yy in range(6, 22):
            for xx in range(36, 48):
                u = 112 + (xx - 32)
                v = 8 + (yy - 6)
                p = s[offset(u, v)]
                s[offset(xx, yy)] = blend_abgr(s[offset(xx, yy)], p)

        return s

    expected = expected_surface()
    for name in ('lifecycle_surface_a', 'lifecycle_surface_b'):
        actual = struct.unpack_from('<32768H', ram, sy[name] - 0x10000000)
        assert actual == tuple(expected), f'{name} playfield, atlas or untouched padding differs in Scene 2'
        for y in range(32):
            for x in range(96, 128):
                if 96 <= x < 104 and 8 <= y < 24:
                    exp_atlas = s2_bgr_a_pixel(x - 96, y - 8)
                elif 104 <= x < 112 and 8 <= y < 24:
                    exp_atlas = s2_abgr_b_pixel(x - 104, y - 8)
                elif 112 <= x < 128 and 8 <= y < 24:
                    exp_atlas = s2_abgr_c_pixel(x - 112, y - 8)
                else:
                    exp_atlas = 0x255A
                assert actual[offset(x, y)] == exp_atlas, f'{name} Scene 2 atlas corrupted at ({x},{y})'

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
    sequence = [sy['lifecycle_surface_a'], sy['lifecycle_surface_b'], sy['video_pixels']]
    for address in addresses:
        if sequence and address == sequence[0]: sequence.pop(0)
    assert not sequence, 'scanout did not visit A, B, then linear recovery'
    banks = [v for v in addresses if v in (sy['lifecycle_surface_a'], sy['lifecycle_surface_b'])]
    assert len(banks) >= 32
    stats = {name: word(name) for name in ('gpu_operation_ticks', 'gpu_present_ticks', 'gpu_total_ticks', 'gpu_submission_ticks', 'gpu_completion_ticks', 'gpu_lists')}
    result.write_text(json.dumps({'status': 'PASS', 'mmu': mmu, 'view': view, 'control': hex(word('gpu_lifecycle_control')), 'stats': stats}, indent=2))
    return f'PASS gpu_lifecycle_{mmu}_{view}: 32 frames, {stats["gpu_lists"]} lists, VRAM flips={len(banks)}, {stats["gpu_operation_ticks"]} render ticks, {stats["gpu_present_ticks"]} present ticks'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', default=str(ROOT / 'build/emulator/flashem'))
    p.add_argument('--bios', default=str(ROOT / 'dumps/A29L160ATV@TSOP48.BIN'))
    p.add_argument('--out', default=str(ROOT / 'build/gpu-lifecycle-acceptance'))
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

