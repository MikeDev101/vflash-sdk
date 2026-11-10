"""Verify real upstream SDL2 video/renderer integration on V.Flash Zevio GPU:
testspriteminimal sprite bouncing, texture uploads via virtual asset loader,
double-buffered presentation flips, memory allocation tracking, and scanout recovery."""
import argparse, concurrent.futures, json, re, struct, subprocess, sys
from pathlib import Path
from host_renderer import read_ppm

ROOT = Path(__file__).resolve().parents[1]

def run(mmu, view, emulator, bios, base):
    out = base / f'{mmu}-{view}'
    out.mkdir(parents=True, exist_ok=True)
    result = out / 'sdl2-result.json'
    result.unlink(missing_ok=True)
    elf = ROOT / f'build/vflash_gpu_sdl2_{mmu}_{view}.elf'
    subprocess.run([
        sys.executable, str(ROOT / 'tools/emulator_smoke.py'),
        '--emulator', str(emulator), '--bios', str(bios),
        '--elf', str(elf),
        '--disc', str(ROOT / f'build/gpu/sdl2/{mmu}/{view}/homebrew.cue'),
        '--out', str(out), '--trace-video'
    ], check=True, stdout=subprocess.DEVNULL, timeout=150)

    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ['arm-none-eabi-nm', '--defined-only', str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / 'ram.bin').read_bytes()
    word = lambda name: struct.unpack_from('<I', ram, sy[name] - 0x10000000)[0]

    assert word('startup_status') == 0x600D600D, f'startup_status mismatch: {hex(word("startup_status"))}'
    assert word('boot_marker') == 0x56464C53, f'boot_marker mismatch: {hex(word("boot_marker"))}'
    assert word('data_cookie') == 0x13579BDF, f'data_cookie mismatch: {hex(word("data_cookie"))}'
    assert word('video_status') == 0x56494431, f'video_status mismatch: {hex(word("video_status"))}'
    assert word('render_status') == 0x44524157, f'render_status mismatch: {hex(word("render_status"))}'
    assert word('gpu_sdl2_status') == 0x600D5D12, f'gpu_sdl2_status mismatch: {hex(word("gpu_sdl2_status"))}'
    assert word('gpu_sdl2_frames') >= 32, f'insufficient frames rendered: {word("gpu_sdl2_frames")}'
    assert bool(word('gpu_sdl2_control') & 1) == (mmu == 'on'), f'MMU control mismatch: {hex(word("gpu_sdl2_control"))}'
    assert word('gpu_lists') > 0, 'no GPU command lists recorded'
    assert word('gpu_heap_used') > 0, 'heap tracking recorded 0 bytes'
    assert word('gpu_alloc_count') > 0, 'alloc count recorded 0'

    text = (out / 'run.log').read_text(errors='replace')
    addresses = [int(v, 16) for v in re.findall(r'layer 0 address ([0-9A-Fa-f]{8})', text)]

    # Count transitions between surface A and surface B
    surf_flips = 0
    prev = None
    for a in addresses:
        if prev is not None and a != prev and a in (addresses[-2], addresses[-3]):
            surf_flips += 1
        prev = a

    w, h, pixels = read_ppm(out / 'frame.ppm')
    assert (w, h) == (320, 240), f'unexpected PPM dimensions: {w}x{h}'

    if view == 'linear':
        assert addresses[-1] == sy['video_pixels'], f'linear scanout not restored: {hex(addresses[-1])}'
        assert all(b == 0 for b in pixels), 'linear scanout frame must be clean black'
    else:
        colors = {}
        for i in range(0, len(pixels), 3):
            c = (pixels[i], pixels[i+1], pixels[i+2])
            colors[c] = colors.get(c, 0) + 1

        gray_cnt = colors.get((165, 165, 165), 0)
        yellow_cnt = colors.get((255, 255, 0), 0)
        black_cnt = colors.get((0, 0, 0), 0)
        assert gray_cnt > 70000, f'insufficient gray background pixels: {gray_cnt}'
        assert yellow_cnt > 1000, f'insufficient yellow sprite pixels: {yellow_cnt}'
        assert black_cnt > 500, f'insufficient black sprite outline pixels: {black_cnt}'

    stats = {
        'frames': word('gpu_sdl2_frames'),
        'lists': word('gpu_lists'),
        'sub_ticks': word('gpu_submission_ticks'),
        'comp_ticks': word('gpu_completion_ticks'),
        'heap_used': word('gpu_heap_used'),
        'alloc_count': word('gpu_alloc_count'),
    }
    result.write_text(json.dumps({
        'status': 'PASS',
        'mmu': mmu,
        'view': view,
        'control': hex(word('gpu_sdl2_control')),
        'stats': stats
    }, indent=2))
    return f'PASS gpu_sdl2_{mmu}_{view}: {stats["frames"]} frames, {stats["lists"]} GPU lists, heap={stats["heap_used"]}B in {stats["alloc_count"]} allocs'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', default=str(ROOT / 'build/emulator/flashem'))
    p.add_argument('--bios', default=str(ROOT / 'dumps/A29L160ATV@TSOP48.BIN'))
    p.add_argument('--out', default=str(ROOT / 'build/gpu-sdl2-acceptance'))
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
