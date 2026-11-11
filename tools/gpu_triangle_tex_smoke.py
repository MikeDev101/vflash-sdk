"""Verify native Zevio GPU 3D textured triangles, perspective-correct sampling, coordinate wrapping, and presentation."""
import argparse, concurrent.futures, json, math, re, struct, subprocess, sys
from pathlib import Path
from host_renderer import read_ppm, to_rgb

ROOT = Path(__file__).resolve().parents[1]

def make_texture():
    tex = [0] * (32 * 32)
    for v in range(32):
        for u in range(32):
            if v < 16:
                base = 0x001F if u < 16 else 0x03E0
            else:
                base = 0x7C00 if u < 16 else 0x03FF
            tex[v * 32 + u] = base
    return tex

def bilinear_sample(u_norm, v_norm, tex, w=32, h=32):
    # Matches FlashEm bilinear sample algorithm
    u_scaled = u_norm * w
    v_scaled = v_norm * h
    u_prime = u_scaled - 0.5
    v_prime = v_scaled - 0.5
    u0 = math.floor(u_prime)
    v0 = math.floor(v_prime)
    fu = u_prime - u0
    fv = v_prime - v0
    u1 = u0 + 1
    v1 = v0 + 1

    t00 = tex[(v0 & (h - 1)) * w + (u0 & (w - 1))]
    t10 = tex[(v0 & (h - 1)) * w + (u1 & (w - 1))]
    t01 = tex[(v1 & (h - 1)) * w + (u0 & (w - 1))]
    t11 = tex[(v1 & (h - 1)) * w + (u1 & (w - 1))]

    if t00 <= 0 or t10 <= 0 or t01 <= 0 or t11 <= 0:
        idx = int(fu >= 0.5) + int(fv >= 0.5) * 2
        return [t00, t10, t01, t11][idx]

    w00 = (1.0 - fu) * (1.0 - fv)
    w10 = fu * (1.0 - fv)
    w01 = (1.0 - fu) * fv
    w11 = fu * fv

    res = 0
    for shift in (0, 5, 10):
        c00 = (t00 >> shift) & 0x1F
        c10 = (t10 >> shift) & 0x1F
        c01 = (t01 >> shift) & 0x1F
        c11 = (t11 >> shift) & 0x1F
        val = int(w00 * c00 + w10 * c10 + w01 * c01 + w11 * c11 + 0.5)
        res |= (val & 0x1F) << shift
    return res

def draw_tex_tri(fb, tex, v0, v1, v2, scissor=None):
    x0, y0, z0, u0, v0 = v0
    x1, y1, z1, u1, v1 = v1
    x2, y2, z2, u2, v2 = v2

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
                b0 = w0 / area
                b1 = w1 / area
                b2 = w2 / area
                inv_z = b0 / z0 + b1 / z1 + b2 / z2
                z_pixel = 1.0 / inv_z
                u_norm = (b0 * (u0 / z0) + b1 * (u1 / z1) + b2 * (u2 / z2)) * z_pixel
                v_norm = (b0 * (v0 / z0) + b1 * (v1 / z1) + b2 * (v2 / z2)) * z_pixel
                texel = bilinear_sample(u_norm, v_norm, tex)
                if texel > 0:
                    fb[y * 320 + x] = texel

def host_expected_surface():
    fb = [0x1063] * (320 * 240)
    tex = make_texture()

    # 1. Affine textured quad: [30..190, 30..190]
    # Triangle 1A: (30,30, 1.0, 0,0) - (30,190, 1.0, 0,1) - (190,190, 1.0, 1,1)
    draw_tex_tri(fb, tex,
                 (30, 30, 1.0, 0.0, 0.0),
                 (30, 190, 1.0, 0.0, 1.0),
                 (190, 190, 1.0, 1.0, 1.0))
    # Triangle 1B: (30,30, 1.0, 0,0) - (190,190, 1.0, 1,1) - (190,30, 1.0, 1,0)
    draw_tex_tri(fb, tex,
                 (30, 30, 1.0, 0.0, 0.0),
                 (190, 190, 1.0, 1.0, 1.0),
                 (190, 30, 1.0, 1.0, 0.0))

    # 2. Perspective foreshortened triangle:
    # Apex (240, 30, Z=1.0), Base BL (180, 150, Z=2.0), Base BR (300, 150, Z=2.0)
    draw_tex_tri(fb, tex,
                 (240, 30, 1.0, 0.5, 0.0),
                 (180, 150, 2.0, 0.0, 1.0),
                 (300, 150, 2.0, 1.0, 1.0))

    # 3. Coordinate wrapping triangle under Scissor Rect [190..300, 160..230]
    draw_tex_tri(fb, tex,
                 (245, 150, 1.0, 1.0, 0.0),
                 (180, 230, 1.0, 0.0, 2.0),
                 (310, 230, 1.0, 2.0, 2.0),
                 scissor=(190, 160, 300, 230))

    return fb

def run(mmu, view, emulator, bios, base):
    out = base / f'{mmu}-{view}'; out.mkdir(parents=True, exist_ok=True)
    result = out / 'triangle-tex-result.json'; result.unlink(missing_ok=True)
    elf = ROOT / f'build/vflash_gpu_triangle_tex_{mmu}_{view}.elf'
    disc = ROOT / f'build/gpu/triangle_tex/{mmu}/{view}/homebrew.cue'

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
    assert word('gpu_triangle_status') == 0x54455831, 'invalid triangle tex status'
    assert word('gpu_triangle_frames') == expected_frames, 'unexpected frame count'
    assert word('gpu_lists') == expected_frames * 3, 'unexpected submitted command list count'
    assert bool(word('gpu_triangle_control') & 1) == (mmu == 'on'), 'MMU state mismatch'

    tiled_offset = lambda x, y: (y // 8) * 8192 + (x // 32) * 256 + (y % 8) * 32 + x % 32
    expected_playfield = host_expected_surface()
    tex = make_texture()

    # Verify Surface A and Surface B playfield and texture atlas
    for name in ('triangle_surface_a', 'triangle_surface_b'):
        s_base = sy[name] - 0x10000000
        # Playfield pixels (320x240)
        exact_matches = 0
        for y in range(240):
            for x in range(320):
                act = struct.unpack_from('<H', ram, s_base + tiled_offset(x, y) * 2)[0]
                exp = expected_playfield[y * 320 + x]
                if act == exp:
                    exact_matches += 1
                else:
                    dr = abs((act & 0x1F) - (exp & 0x1F))
                    dg = abs(((act >> 5) & 0x1F) - ((exp >> 5) & 0x1F))
                    db = abs(((act >> 10) & 0x1F) - ((exp >> 10) & 0x1F))
                    assert dr <= 1 and dg <= 1 and db <= 1, \
                        f'{name} mismatch at ({x},{y}): expected 0x{exp:04X}, observed 0x{act:04X}'
        assert exact_matches >= 76700, f'{name}: too few exact pixel matches ({exact_matches}/76800)'

        # Playfield offscreen row-stride padding must remain sentinel 0x255A
        for y in range(240):
            for x in range(320, 1024):
                act = struct.unpack_from('<H', ram, s_base + tiled_offset(x, y) * 2)[0]
                assert act == 0x255A, f'{name} offscreen padding corrupted at ({x},{y}): 0x{act:04X}'

        # Offscreen texture atlas at (0..31, 240..271) must contain the exact uploaded texture
        for v in range(32):
            for u in range(32):
                act = struct.unpack_from('<H', ram, s_base + tiled_offset(u, 240 + v) * 2)[0]
                exp = tex[v * 32 + u]
                assert act == exp, f'{name} texture atlas corrupted at ({u},{v}): expected 0x{exp:04X}, observed 0x{act:04X}'

    # CPU linear framebuffer must remain completely black (untouched)
    assert ram[sy['video_pixels'] - 0x10000000:][:153600] == bytes(153600), 'linear framebuffer modified'

    # Verify actual presented scanout
    expected_scanout = [0] * 76800
    if view != 'linear':
        target_name = 'triangle_surface_a' if view == 'a' else 'triangle_surface_b'
        target_base = sy[target_name] - 0x10000000
        expected_scanout = [
            struct.unpack_from('<H', ram, target_base + tiled_offset(x, y) * 2)[0]
            for y in range(240) for x in range(320)
        ]
    assert read_ppm(out / 'frame.ppm') == (320, 240, to_rgb(struct.pack('<76800H', *expected_scanout))), \
        'actual scanout mismatch against surface pixels in SDRAM'

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
        features=['Textured Vertex Format (0x44, vtx_fmt 0x10)',
                  'Texture Binding (0x86 size/format, 0x87 atlas origin)',
                  'Affine Texture Mapping (uniform Z=1.0f)',
                  'Perspective-Correct Texture Mapping (varying Z)',
                  'Coordinate Wrapping (UV > 1.0)',
                  'Scissor Clipping'],
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
