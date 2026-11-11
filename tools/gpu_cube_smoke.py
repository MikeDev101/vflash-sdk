"""Verify native Zevio 3D hardware-transformed rotating textured cube and scanout presentation."""
import argparse, concurrent.futures, json, math, re, struct, subprocess, sys
from pathlib import Path
from host_renderer import read_ppm, to_rgb

ROOT = Path(__file__).resolve().parents[1]

def f16_from_q14(val):
    if val == 0:
        return 0
    sign = 0x8000 if val < 0 else 0
    u = -val if val < 0 else val
    highest = u.bit_length() - 1
    exp_biased = highest - 14 + 31
    if highest >= 9:
        round_bit = 1 << (highest - 10) if highest >= 10 else 0
        mant = ((u + round_bit) >> (highest - 9)) & 0x1FF
    else:
        mant = (u << (9 - highest)) & 0x1FF
    return sign | (exp_biased << 9) | mant

def float_from_f16(h):
    if h == 0:
        return 0.0
    sign = -1.0 if (h & 0x8000) else 1.0
    exp = ((h >> 9) & 0x3F) - 31
    mant = (h & 0x1FF) / 512.0
    return sign * (1.0 + mant) * (2.0 ** exp)

SIN_TABLE = []
for i in range(256):
    rad = i * 2.0 * math.pi / 256.0
    val = int(round(math.sin(rad) * 16384.0))
    if val > 16384:
        val = 16384
    if val < -16384:
        val = -16384
    SIN_TABLE.append(val)

def make_cube_texture():
    tex = [0x5555] * (64 * 32)
    for v in range(32):
        for u in range(64):
            val = 0x5555
            if v < 16:
                if u < 16:
                    val = 0x03FF if (u < 2 or u >= 14 or v < 2 or v >= 14) else 0x001F
                elif u < 32:
                    lu = u - 16
                    val = 0x7FE0 if (((lu // 4) ^ (v // 4)) & 1) else 0x7C00
                elif u < 48:
                    lu = u - 32
                    val = 0x7FFF if (lu in (7, 8) or v in (7, 8)) else 0x03E0
                else:
                    lu = u - 48
                    d = (lu - 8 if lu >= 8 else 7 - lu) + (v - 8 if v >= 8 else 7 - v)
                    val = 0x03FF if (d <= 4) else 0x7C1F
            else:
                lv = v - 16
                if u < 16:
                    val = 0x7FFF if (5 <= u <= 10 and 5 <= lv <= 10) else 0x7C00
                elif u < 32:
                    val = 0x001F if ((lv // 2) & 1) else 0x03FF
            tex[v * 64 + u] = val
    return tex

def bilinear_sample(u_norm, v_norm, tex, w=64, h=32):
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

F16_POS1 = 0x3E00
F16_NEG1 = 0xBE00
F16_0_00 = 0x0000
F16_0_25 = 0x3A00
F16_0_50 = 0x3C00
F16_0_75 = 0x3D00
F16_1_00 = 0x3E00

QUADS = [
    ('front',  (-1,-1,-1), (+1,-1,-1), (+1,+1,-1), (-1,+1,-1), F16_0_00, F16_0_25, F16_0_00, F16_0_50),
    ('back',   (+1,-1,+1), (-1,-1,+1), (-1,+1,+1), (+1,+1,+1), F16_0_25, F16_0_50, F16_0_00, F16_0_50),
    ('left',   (-1,-1,+1), (-1,-1,-1), (-1,+1,-1), (-1,+1,+1), F16_0_50, F16_0_75, F16_0_00, F16_0_50),
    ('right',  (+1,-1,-1), (+1,-1,+1), (+1,+1,+1), (+1,+1,-1), F16_0_75, F16_1_00, F16_0_00, F16_0_50),
    ('top',    (-1,-1,+1), (+1,-1,+1), (+1,-1,-1), (-1,-1,-1), F16_0_00, F16_0_25, F16_0_50, F16_1_00),
    ('bottom', (-1,+1,-1), (+1,+1,-1), (+1,+1,+1), (-1,+1,+1), F16_0_25, F16_0_50, F16_0_50, F16_1_00),
]

VERTICES = []
for name, tl, tr, br, bl, u0, u1, v0, v1 in QUADS:
    corners = [(tl, u0, v0), (tr, u1, v0), (br, u1, v1), (tl, u0, v0), (br, u1, v1), (bl, u0, v1)]
    for (x, y, z), u, v in corners:
        fx = F16_POS1 if x > 0 else F16_NEG1
        fy = F16_POS1 if y > 0 else F16_NEG1
        fz = F16_POS1 if z > 0 else F16_NEG1
        VERTICES.append((fx, fy, fz, u, v))

def raster_cube_frame(frame, tex):
    fb = [0x1063] * (320 * 240)
    zbuf = [1e30] * (320 * 240)

    ay = (frame * 8) & 0xFF
    ax = (frame * 5) & 0xFF
    sy, cy = SIN_TABLE[ay], SIN_TABLE[(ay + 64) & 0xFF]
    sx, cx = SIN_TABLE[ax], SIN_TABLE[(ax + 64) & 0xFF]

    m00 = float_from_f16(f16_from_q14(cy))
    m01 = 0.0
    m02 = float_from_f16(f16_from_q14(sy))
    m10 = float_from_f16(f16_from_q14((sx * sy) >> 14))
    m11 = float_from_f16(f16_from_q14(cx))
    m12 = float_from_f16(f16_from_q14(-(sx * cy) >> 14))
    m20 = float_from_f16(f16_from_q14(-(cx * sy) >> 14))
    m21 = float_from_f16(f16_from_q14(sx))
    m22 = float_from_f16(f16_from_q14((cx * cy) >> 14))

    proj = []
    for fx, fy, fz, fu, fv in VERTICES:
        vx = float_from_f16(fx)
        vy = float_from_f16(fy)
        vz = float_from_f16(fz)
        x1 = vx * m00 + vy * m01 + vz * m02 + 0.0
        y1 = vx * m10 + vy * m11 + vz * m12 + 0.0
        z1 = vx * m20 + vy * m21 + vz * m22 + 3.5
        sx_proj = 160.0 + (x1 * 160.0) / z1
        sy_proj = 120.0 + (y1 * 160.0) / z1
        u_norm = float_from_f16(fu)
        v_norm = float_from_f16(fv)
        proj.append((sx_proj, sy_proj, z1, u_norm, v_norm))

    for i in range(0, 36, 3):
        v0, v1, v2 = proj[i], proj[i+1], proj[i+2]
        x0, y0, z0, u0, v0_uv = v0
        x1, y1, z1, u1, v1_uv = v1
        x2, y2, z2, u2, v2_uv = v2

        area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
        if abs(area) < 1e-6:
            continue

        min_x = max(0, int(math.floor(min(x0, x1, x2))))
        max_x = min(319, int(math.ceil(max(x0, x1, x2))))
        min_y = max(0, int(math.floor(min(y0, y1, y2))))
        max_y = min(239, int(math.ceil(max(y0, y1, y2))))

        for y in range(min_y, max_y + 1):
            for x in range(min_x, max_x + 1):
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
                    if z_pixel < zbuf[y * 320 + x]:
                        u_p = (b0 * (u0 / z0) + b1 * (u1 / z1) + b2 * (u2 / z2)) * z_pixel
                        v_p = (b0 * (v0_uv / z0) + b1 * (v1_uv / z1) + b2 * (v2_uv / z2)) * z_pixel
                        texel = bilinear_sample(u_p, v_p, tex)
                        if texel > 0:
                            zbuf[y * 320 + x] = z_pixel
                            fb[y * 320 + x] = texel
    return fb

def run(mmu, view, emulator, bios, base):
    out = base / f'{mmu}-{view}'; out.mkdir(parents=True, exist_ok=True)
    result = out / 'cube-result.json'; result.unlink(missing_ok=True)
    elf = ROOT / f'build/vflash_gpu_cube_{mmu}_{view}.elf'
    disc = ROOT / f'build/gpu/cube/{mmu}/{view}/homebrew.cue'

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
    assert word('gpu_cube_status') == 0x43554231, 'invalid cube status'
    assert word('gpu_cube_frames') == expected_frames, 'unexpected frame count'
    assert word('gpu_lists') == expected_frames, 'unexpected submitted command list count'
    assert bool(word('gpu_cube_control') & 1) == (mmu == 'on'), 'MMU state mismatch'

    tiled_offset = lambda x, y: (y // 8) * 8192 + (x // 32) * 256 + (y % 8) * 32 + x % 32
    tex = make_cube_texture()

    # Determine final frame drawn on each surface:
    # Frame 0 -> A, 1 -> B, ..., 31 -> B, 32 -> A
    final_frame_a = 32 if view == 'a' else 30
    final_frame_b = 31

    expected_surface_a = raster_cube_frame(final_frame_a, tex)
    expected_surface_b = raster_cube_frame(final_frame_b, tex)

    # Verify Surface A and Surface B playfield, padding, and texture atlas
    for name, exp_fb in (('cube_surface_a', expected_surface_a), ('cube_surface_b', expected_surface_b)):
        s_base = sy[name] - 0x10000000
        exact_matches = 0
        for y in range(240):
            for x in range(320):
                act = struct.unpack_from('<H', ram, s_base + tiled_offset(x, y) * 2)[0]
                exp = exp_fb[y * 320 + x]
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

        # Offscreen texture atlas at (0..63, 240..271) must contain exact texture
        for v in range(32):
            for u in range(64):
                act = struct.unpack_from('<H', ram, s_base + tiled_offset(u, 240 + v) * 2)[0]
                exp = tex[v * 64 + u]
                assert act == exp, f'{name} texture atlas corrupted at ({u},{v}): expected 0x{exp:04X}, observed 0x{act:04X}'

    # CPU linear framebuffer must remain completely black (untouched)
    assert ram[sy['video_pixels'] - 0x10000000:][:153600] == bytes(153600), 'linear framebuffer modified'

    # Verify actual presented scanout matches SDRAM surface pixels
    expected_scanout = [0] * 76800
    if view != 'linear':
        target_name = 'cube_surface_a' if view == 'a' else 'cube_surface_b'
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
        sequence = [sy['cube_surface_a'], sy['cube_surface_b'], sy['video_pixels']]
        for address in addresses:
            if sequence and address == sequence[0]:
                sequence.pop(0)
        assert not sequence, 'scanout did not visit A, B, then linear recovery'
    else:
        target_surface = sy['cube_surface_a'] if view == 'a' else sy['cube_surface_b']
        assert addresses and addresses[-1] == target_surface, f'final scanout address did not remain at {view}'

    stats = {name: word(name) for name in (
        'gpu_operation_ticks', 'gpu_present_ticks', 'gpu_total_ticks',
        'gpu_submission_ticks', 'gpu_completion_ticks', 'gpu_lists'
    )}
    assert stats['gpu_total_ticks'] >= stats['gpu_present_ticks'] > 0

    result.write_text(json.dumps(dict(
        passed=True, mmu=mmu, view=view, frames=expected_frames,
        surface='320x240 direct tiled double-buffered scanout',
        features=['Hardware 3D Transform Pipeline (Scale, Matrix 1 Rotation, Translation 1)',
                  'Perspective-Correct Projection and UV Mapping',
                  'Depth Testing and Clear (0x1E, 0x85, D3 0xD3000003, 0x96)',
                  'Hardware Bilinear Texture Filtering (64x32 Atlas, 6 Distinct Faces)',
                  'Double-Buffered Presentation & Clean Linear Restoration'],
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

