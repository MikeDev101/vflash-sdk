"""Verify native GPU alpha blending (ARGB4444 and BGR555 constant blending), clipping, and scanout."""
import argparse,concurrent.futures,json,re,struct,subprocess,sys
from pathlib import Path
from host_renderer import read_ppm,to_rgb
ROOT=Path(__file__).resolve().parents[1]

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

def run(mmu, view, emulator, bios, base):
    out = base / f'{mmu}-{view}'
    out.mkdir(parents=True, exist_ok=True)
    result = out / 'alpha-result.json'
    result.unlink(missing_ok=True)
    elf = ROOT / f'build/vflash_gpu_alpha_{mmu}_{view}.elf'
    subprocess.run([
        sys.executable, str(ROOT / 'tools/emulator_smoke.py'),
        '--emulator', str(emulator), '--bios', str(bios),
        '--elf', str(elf),
        '--disc', str(ROOT / f'build/gpu/alpha/{mmu}/{view}/homebrew.cue'),
        '--out', str(out), '--trace-video'
    ], check=True, stdout=subprocess.DEVNULL, timeout=150)
    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ['arm-none-eabi-nm', '--defined-only', str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / 'ram.bin').read_bytes()
    word = lambda name: struct.unpack_from('<I', ram, sy[name] - 0x10000000)[0]
    assert word('gpu_alpha_status') == 0x47414C31 and word('gpu_alpha_frames') == 32
    assert word('gpu_alpha_uploads') == 4 and word('gpu_lists') == 608
    assert bool(word('gpu_alpha_control') & 1) == (mmu == 'on')

    # Verify linear assets in SDRAM are unmodified
    expected_argb = tuple(
        (0x0000 if y < 4 else (0x7FFF if y < 8 else (0x3FFF if y < 12 else 0xFFFF)))
        for y in range(16) for x in range(16)
    )
    expected_bgr = tuple(
        (0x8000 if y < 4 else 0x7FFF)
        for y in range(16) for x in range(16)
    )
    assert struct.unpack_from('<256H', ram, sy['alpha_argb_asset'] - 0x10000000) == expected_argb, 'ARGB linear asset modified'
    assert struct.unpack_from('<256H', ram, sy['alpha_bgr_asset'] - 0x10000000) == expected_bgr, 'BGR linear asset modified'

    offset = lambda x, y: (y // 8) * 8192 + (x // 32) * 256 + (y % 8) * 32 + x % 32

    def expected_surface():
        s = [0x255A] * 32768
        def fill(x, y, w, h, c):
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    s[offset(xx, yy)] = c
        # Atlas at (96, 8) and (112, 8)
        for y in range(16):
            for x in range(16):
                s[offset(96 + x, 8 + y)] = expected_argb[y * 16 + x]
                s[offset(112 + x, 8 + y)] = expected_bgr[y * 16 + x]
        # Playfield 4 quadrants
        fill(0, 0, 48, 16, 0x0000)
        fill(48, 0, 48, 16, 0x001F)
        fill(0, 16, 48, 16, 0x03E0)
        fill(48, 16, 48, 16, 0x7C00)

        # Helper to simulate sprite blit
        def draw_sprite(sx, sy, w, h, dx, dy, cx, cy, cw, ch, is_4444, blend_mode, blend_k):
            l = max(dx, cx)
            t = max(dy, cy)
            r = min(dx + w, cx + cw)
            b = min(dy + h, cy + ch)
            for yy in range(t, b):
                for xx in range(l, r):
                    u = sx + (xx - dx)
                    v = sy + (yy - dy)
                    raw = s[offset(u, v)]
                    if is_4444:
                        a = raw >> 12
                        if not a:
                            continue
                        r_ch = (raw & 15)
                        g_ch = ((raw >> 4) & 15)
                        b_ch = ((raw >> 8) & 15)
                        bgr = ((r_ch << 1) | (r_ch >> 3)) | (((g_ch << 1) | (g_ch >> 3)) << 5) | (((b_ch << 1) | (b_ch >> 3)) << 10)
                        c_val = bgr | ((a + 1) << 16)
                    else:
                        if not raw or (raw & 0x8000):
                            continue
                        c_val = raw | (16 << 16)
                    s[offset(xx, yy)] = blend_px(s[offset(xx, yy)], c_val, blend_mode, blend_k)

        # 1. Unclipped ARGB4444 at (8, 0), (56, 0), (8, 16), (56, 16)
        for dx, dy in [(8, 0), (56, 0), (8, 16), (56, 16)]:
            draw_sprite(96, 8, 16, 16, dx, dy, 0, 0, 96, 32, True, 0, 0)

        # 2. Unclipped BGR555 at (28, 0), (76, 0), (28, 16), (76, 16)
        for dx, dy in [(28, 0), (76, 0), (28, 16), (76, 16)]:
            draw_sprite(112, 8, 16, 8, dx, dy, 0, 0, 96, 32, False, 2, 0x808080)
            draw_sprite(112, 16, 16, 4, dx, dy + 8, 0, 0, 96, 32, False, 2, 0x404040)
            draw_sprite(112, 20, 16, 4, dx, dy + 12, 0, 0, 96, 32, False, 0, 0)

        # 3. Clipped ARGB4444 at (-4, 2) clipped to (1, 2, 6, 12)
        draw_sprite(96, 8, 16, 16, -4, 2, 1, 2, 6, 12, True, 0, 0)

        # 4. Clipped BGR555 at (-4, 18) clipped to (1, 18, 6, 12)
        draw_sprite(112, 8, 16, 8, -4, 18, 1, 18, 6, 12, False, 2, 0x808080)
        draw_sprite(112, 16, 16, 4, -4, 26, 1, 18, 6, 12, False, 2, 0x404040)

        return s

    expected = expected_surface()
    for name in ['alpha_surface_a', 'alpha_surface_b']:
        actual = struct.unpack_from('<32768H', ram, sy[name] - 0x10000000)
        assert actual == tuple(expected), f'{name} playfield, atlas, or padding differs'
        # Verify specific expected pixel values on both unclipped and clipped regions
        # Quad 0: Black (y=4..7 is 50% white -> 0x3DEF, y=8..11 is 25% white -> 0x1CE7)
        for y in range(4, 8):
            for x in range(8, 24):
                assert actual[offset(x, y)] == 0x3DEF, f'ARGB 50% black failed at ({x},{y}): got 0x{actual[offset(x,y)]:04X}'
            for x in range(28, 44):
                assert actual[offset(x, y)] == 0x3DEF, f'BGR 50% black failed at ({x},{y}): got 0x{actual[offset(x,y)]:04X}'
        # Quad 1: Red (y=4..7 is 50% white -> 0x3DFF)
        for y in range(4, 8):
            for x in range(56, 72):
                assert actual[offset(x, y)] == 0x3DFF, f'ARGB 50% red failed at ({x},{y}): got 0x{actual[offset(x,y)]:04X}'
            for x in range(76, 92):
                assert actual[offset(x, y)] == 0x3DFF, f'BGR 50% red failed at ({x},{y}): got 0x{actual[offset(x,y)]:04X}'
        # Quad 2: Green (y=4..7 is 50% white -> 0x3FEF)
        for y in range(20, 24):
            for x in range(8, 24):
                assert actual[offset(x, y)] == 0x3FEF, f'ARGB 50% green failed at ({x},{y}): got 0x{actual[offset(x,y)]:04X}'
            for x in range(28, 44):
                assert actual[offset(x, y)] == 0x3FEF, f'BGR 50% green failed at ({x},{y}): got 0x{actual[offset(x,y)]:04X}'
        # Quad 3: Blue (y=4..7 is 50% white -> 0x7DEF)
        for y in range(20, 24):
            for x in range(56, 72):
                assert actual[offset(x, y)] == 0x7DEF, f'ARGB 50% blue failed at ({x},{y}): got 0x{actual[offset(x,y)]:04X}'
            for x in range(76, 92):
                assert actual[offset(x, y)] == 0x7DEF, f'BGR 50% blue failed at ({x},{y}): got 0x{actual[offset(x,y)]:04X}'

    assert ram[sy['video_pixels'] - 0x10000000:][:153600] == bytes(153600), 'CPU linear buffer must remain black'

    # Check direct scanout PPM
    expected_scanout = [0] * 76800
    if view != 'linear':
        source = expected
        ox, oy = (0, 0) if view == 'a' else (224, 208)
        for y in range(32):
            for x in range(96):
                expected_scanout[(oy + y) * 320 + ox + x] = source[offset(x, y)]
    assert read_ppm(out / 'frame.ppm') == (320, 240, to_rgb(struct.pack('<76800H', *expected_scanout))), 'direct viewport scanout mismatch'

    # Verify scanout sequence
    text = (out / 'run.log').read_text(errors='replace')
    addresses = [int(v, 16) for v in re.findall(r'layer 0 address ([0-9A-Fa-f]{8})', text)]
    sequence = [sy['alpha_surface_a'], sy['alpha_surface_b'], sy['video_pixels']]
    for address in addresses:
        if sequence and address == sequence[0]:
            sequence.pop(0)
    assert not sequence, 'scanout did not visit A, B, then linear recovery'
    banks = [v for v in addresses if v in (sy['alpha_surface_a'], sy['alpha_surface_b'])]
    assert len(banks) >= 32 and banks[:32] == [sy['alpha_surface_a'] if i % 2 == 0 else sy['alpha_surface_b'] for i in range(32)]

    stats = {name: word(name) for name in ('gpu_operation_ticks', 'gpu_present_ticks', 'gpu_total_ticks', 'gpu_submission_ticks', 'gpu_completion_ticks', 'gpu_lists', 'gpu_alpha_uploads')}
    assert stats['gpu_total_ticks'] >= stats['gpu_present_ticks'] > 0
    result.write_text(json.dumps(dict(
        passed=True, mmu=mmu, view=view, frames=32,
        test='ARGB4444 (0x86 bit 8) and BGR555 constant blending (0x88/0x8D) over Black, Red, Green, Blue',
        exact_50_pct=dict(black='0x3DEF', red='0x3DFF', green='0x3FEF', blue='0x7DEF'),
        exact_25_pct=dict(black='0x1CE7', red='0x1CFF', green='0x1FE7', blue='0x7CE7'),
        exact_100_pct='0x7FFF', transparent='untouched_background',
        statistics=stats, timer_resolution_us=100, cpu_conversion=False
    ), indent=2) + '\n')
    print('PASS', mmu, view, flush=True)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', type=Path, required=True)
    p.add_argument('--bios', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(run, m, v, a.emulator, a.bios, a.out) for m in ('off', 'on') for v in ('a', 'b', 'linear')]
        for f in futures:
            f.result()

if __name__ == '__main__':
    main()

