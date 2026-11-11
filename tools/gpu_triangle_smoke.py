"""Verify native Zevio GPU 3D triangle rendering, winding, clipping, and presentation."""
import argparse, concurrent.futures, json, re, struct, subprocess, sys
from pathlib import Path
from host_renderer import read_ppm, to_rgb

ROOT = Path(__file__).resolve().parents[1]

def draw_tri(fb, v0, v1, v2, color, scissor=None):
    x0, y0 = v0; x1, y1 = v1; x2, y2 = v2
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
    sc_x0, sc_y0, sc_x1, sc_y1 = scissor if scissor else (0, 0, 319, 239)
    for y in range(sc_y0, sc_y1 + 1):
        for x in range(sc_x0, sc_x1 + 1):
            px = x + 0.5; py = y + 0.5
            w0 = (x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)
            w1 = (x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)
            w2 = (x0 - px) * (y1 - py) - (y0 - py) * (x1 - px)
            inside = (w0 <= 0 and w1 <= 0 and w2 <= 0) if area < 0 else (w0 >= 0 and w1 >= 0 and w2 >= 0)
            if inside:
                fb[y * 320 + x] = color

def host_expected_surface():
    fb = [0x1063] * (320 * 240)
    # Triangle 1: CCW Solid Red
    draw_tri(fb, (160, 40), (80, 180), (240, 180), 0x001F)
    # Triangle 2: CW Solid Green
    draw_tri(fb, (30, 30), (90, 30), (60, 90), 0x03E0)
    # Triangle 3: Solid Blue clipped by scissor
    draw_tri(fb, (250, 30), (180, 140), (320, 140), 0x7C00, scissor=(200, 20, 299, 119))
    return fb

def run(mmu, view, emulator, bios, base):
    out = base / f'{mmu}-{view}'; out.mkdir(parents=True, exist_ok=True)
    result = out / 'triangle-result.json'; result.unlink(missing_ok=True)
    elf = ROOT / f'build/vflash_gpu_triangle_{mmu}_{view}.elf'
    disc = ROOT / f'build/gpu/triangle/{mmu}/{view}/homebrew.cue'

    subprocess.run([
        sys.executable, str(ROOT / 'tools/emulator_smoke.py'),
        '--emulator', str(emulator), '--bios', str(bios),
        '--elf', str(elf), '--disc', str(disc),
        '--out', str(out), '--trace-video'
    ], check=True, stdout=subprocess.DEVNULL, timeout=150)

    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ['arm-none-eabi-nm', '--defined-only', str(elf)], text=True).splitlines() if len(p := line.split()) == 3}

    ram = (out / 'ram.bin').read_bytes()
    word = lambda name: struct.unpack_from('<I', ram, sy[name] - 0x10000000)[0]

    expected_frames = 33 if view == 'a' else 32
    assert word('gpu_triangle_status') == 0x54524931, 'invalid triangle status'
    assert word('gpu_triangle_frames') == expected_frames, 'unexpected frame count'
    assert word('gpu_lists') == expected_frames * 3, 'unexpected submitted command list count'
    assert bool(word('gpu_triangle_control') & 1) == (mmu == 'on'), 'MMU state mismatch'

    tiled_offset = lambda x, y: (y // 8) * 8192 + (x // 32) * 256 + (y % 8) * 32 + x % 32
    expected_playfield = host_expected_surface()

    # Verify Surface A and Surface B playfield and untouched padding
    for name in ('triangle_surface_a', 'triangle_surface_b'):
        s_base = sy[name] - 0x10000000
        # Playfield pixels
        for y in range(240):
            for x in range(320):
                act = struct.unpack_from('<H', ram, s_base + tiled_offset(x, y) * 2)[0]
                exp = expected_playfield[y * 320 + x]
                assert act == exp, f'{name} mismatch at ({x},{y}): expected 0x{exp:04X}, observed 0x{act:04X}'
        # Untouched row-stride padding must remain sentinel 0x255A
        for y in range(240):
            for x in range(320, 1024):
                act = struct.unpack_from('<H', ram, s_base + tiled_offset(x, y) * 2)[0]
                assert act == 0x255A, f'{name} offscreen padding corrupted at ({x},{y}): 0x{act:04X}'

    # CPU linear framebuffer must remain completely black (untouched)
    assert ram[sy['video_pixels'] - 0x10000000:][:153600] == bytes(153600), 'linear framebuffer modified'

    # Verify actual presented scanout
    expected_scanout = [0] * 76800
    if view != 'linear':
        expected_scanout = expected_playfield
    assert read_ppm(out / 'frame.ppm') == (320, 240, to_rgb(struct.pack('<76800H', *expected_scanout))), \
        'actual scanout mismatch against expected raster oracle'

    # Verify scanout address transitions
    text = (out / 'run.log').read_text(errors='replace')
    addresses = [int(v, 16) for v in re.findall(r'layer 0 address ([0-9A-Fa-f]{8})', text)]
    if view == 'linear':
        sequence = [sy['triangle_surface_a'], sy['triangle_surface_b'], sy['video_pixels']]
        for address in addresses:
            if sequence and address == sequence[0]:
                sequence.pop(0)
        assert not sequence, 'scanout did not visit A, B, then linear recovery'
    else:
        target_surface = sy['triangle_surface_a'] if view == 'a' else sy['triangle_surface_b']
        assert addresses and addresses[-1] == target_surface, f'final scanout address did not remain at {view}'

    stats = {name: word(name) for name in (
        'gpu_operation_ticks', 'gpu_present_ticks', 'gpu_total_ticks',
        'gpu_submission_ticks', 'gpu_completion_ticks', 'gpu_lists'
    )}
    assert stats['gpu_total_ticks'] >= stats['gpu_present_ticks'] > 0

    result.write_text(json.dumps(dict(
        passed=True, mmu=mmu, view=view, frames=expected_frames,
        surface='320x240 direct tiled double-buffered scanout',
        triangles=['CCW Solid Red (160,40)-(80,180)-(240,180)',
                   'CW Solid Green (30,30)-(90,30)-(60,90)',
                   'Scissor-Clipped Solid Blue [200..299, 20..119]'],
        statistics=stats, timer_resolution_us=100
    ), indent=2) + '\n')
    print('PASS', mmu, view, flush=True)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', type=Path, required=True)
    p.add_argument('--bios', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(run, m, v, a.emulator, a.bios, a.out)
                   for m in ('off', 'on') for v in ('a', 'b', 'linear')]
        for f in futures:
            f.result()

if __name__ == '__main__':
    main()

