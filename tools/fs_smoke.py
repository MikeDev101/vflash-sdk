"""Verify native V.Flash ISO 9660 filesystem HAL and upstream SDL2 RWops file I/O.

Tests both the native HAL (ISO 9660 PVD mounting, case-insensitive path lookup,
nested directory traversal, sequential & multi-sector reads, SEEK_SET/CUR/END,
EOF handling, multiple open handles, and error reporting) and upstream SDL2 file I/O
(SDL_RWFromFile, SDL_LoadBMP, SDL_LoadWAV, and interactive asset rendering/audio)
under firmware-booted FlashEm across MMU off and on.
"""
import argparse
import concurrent.futures
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run_native(mmu, emulator, bios, base):
    out = base / f"native-{mmu}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_fs_{mmu}.elf"
    disc = ROOT / f"build/fs/native/{mmu}/homebrew.cue"

    subprocess.run([
        sys.executable, str(ROOT / "tools/emulator_smoke.py"),
        "--emulator", str(emulator), "--bios", str(bios),
        "--elf", str(elf), "--disc", str(disc),
        "--frames", "600",
        "--out", str(out),
    ], check=True, stdout=subprocess.DEVNULL, timeout=180)

    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]

    err = word("fs_demo_err")
    passed = word("fs_demo_tests_passed")
    assert word("startup_status") == 0x600D600D, f"startup_status mismatch: {hex(word('startup_status'))}"
    assert err == 0, f"fs_demo_err: {hex(err)} (tests_passed={passed})"
    assert passed == 8, f"fs_demo_tests_passed mismatch: {passed}"
    assert word("fs_demo_active") == 1, "fs_demo not active"
    assert word("fs_demo_bytes_read") == 6308, f"fs_demo_bytes_read mismatch: {word('fs_demo_bytes_read')}"
    assert word("fs_demo_loop_ticks") > 0, "fs_demo did not enter infinite loop"

    stats = {
        "tests_passed": passed,
        "bytes_read": word("fs_demo_bytes_read"),
        "err": err,
        "loop_ticks": word("fs_demo_loop_ticks"),
    }
    result.write_text(json.dumps(dict(
        passed=True, type="native", mmu=mmu, stats=stats
    ), indent=2) + "\n")
    print(f"PASS native_fs_{mmu}: {passed}/8 tests passed, bytes_read={stats['bytes_read']}, err=0x{err:x}", flush=True)


def run_sdl2(mmu, emulator, bios, base):
    out = base / f"sdl2-{mmu}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_sdl2_asset_{mmu}.elf"
    disc = ROOT / f"build/fs/sdl2/{mmu}/homebrew.cue"

    subprocess.run([
        sys.executable, str(ROOT / "tools/emulator_smoke.py"),
        "--emulator", str(emulator), "--bios", str(bios),
        "--elf", str(elf), "--disc", str(disc),
        "--frames", "800",
        "--out", str(out),
    ], check=True, stdout=subprocess.DEVNULL, timeout=180)

    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]

    err = word("asset_demo_err")
    assert word("startup_status") == 0x600D600D, f"startup_status mismatch: {hex(word('startup_status'))}"
    assert word("video_status") == 0x56494431, f"video_status mismatch: {hex(word('video_status'))}"
    assert word("render_status") == 0x44524157, f"render_status mismatch: {hex(word('render_status'))}"
    assert err == 0, f"asset_demo_err: {hex(err)}"
    assert word("asset_demo_cfg_loaded") == 1, "config.txt not loaded via SDL_RWFromFile"
    assert word("asset_demo_bmp_loaded") == 1, "test.bmp not loaded via SDL_LoadBMP"
    assert word("asset_demo_wav_loaded") == 1, "test.wav not loaded via SDL_LoadWAV"
    assert word("asset_demo_active") == 1, "asset_demo not active"
    assert word("asset_demo_frames") >= 10, f"insufficient frames rendered: {word('asset_demo_frames')}"

    ppm = (out / "frame.ppm").read_bytes().split(b"\n", 3)[3]
    unique_colors = len({(ppm[i], ppm[i + 1], ppm[i + 2]) for i in range(0, len(ppm), 3)})
    assert unique_colors >= 100, f"expected rendered BMP sprite colors in frame.ppm, got {unique_colors}"

    stats = {
        "frames": word("asset_demo_frames"),
        "cfg_loaded": word("asset_demo_cfg_loaded"),
        "bmp_loaded": word("asset_demo_bmp_loaded"),
        "wav_loaded": word("asset_demo_wav_loaded"),
        "unique_colors": unique_colors,
        "err": err,
    }
    result.write_text(json.dumps(dict(
        passed=True, type="sdl2", mmu=mmu, stats=stats
    ), indent=2) + "\n")
    print(f"PASS sdl2_asset_{mmu}: {stats['frames']} frames, cfg={stats['cfg_loaded']}, bmp={stats['bmp_loaded']}, wav={stats['wav_loaded']}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, default=ROOT / "build/emulator/flashem")
    parser.add_argument("--bios", type=Path, default=ROOT / "dumps/A29L160ATV@TSOP48.BIN")
    parser.add_argument("--out", type=Path, default=ROOT / "build/fs-acceptance")
    parser.add_argument("--parallel", type=int, default=4)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.parallel) as pool:
        futures = []
        for mmu in ("off", "on"):
            futures.append(pool.submit(run_native, mmu, args.emulator, args.bios, args.out))
            futures.append(pool.submit(run_sdl2, mmu, args.emulator, args.bios, args.out))

        for f in futures:
            f.result()

    print("\nALL 4/4 FILESYSTEM ACCEPTANCE TESTS PASSED (Native HAL + SDL2 RWops across MMU off and on)!")


if __name__ == "__main__":
    main()
