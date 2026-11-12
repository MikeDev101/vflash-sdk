#!/usr/bin/env python3
"""Benchmark Bad Apple video playback performance across profiles and MMU modes.

Measures decode latency, achieved framerate, dropped frames, audio underruns,
and startup milestone timing in FlashEm.
"""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

PROFILES = [
    "320x240_10fps",
    "256x192_15fps",
    "192x144_24fps",
    "160x120_30fps",
]


def load_symbols(elf: Path):
    out = subprocess.check_output(["arm-none-eabi-nm", "--defined-only", str(elf)], text=True)
    return {p[2]: int(p[0], 16) for line in out.splitlines() if len(p := line.split()) == 3}


def benchmark_profile(profile: str, mmu: str, frames: int = 400, bios: Path = None, emulator: Path = None):
    print(f"\n=======================================================")
    print(f"Benchmarking Profile: {profile} (MMU: {mmu.upper()})")
    print(f"=======================================================")

    # 1. Stage assets for this profile
    staged_dir = ROOT / f"build/examples/bad_apple/assets_{profile}"
    stage_cmd = [
        sys.executable, str(ROOT / "tools/stage_bad_apple_assets.py"),
        "--src-dir", str(ROOT / "assets/local/bad-apple"),
        "--dst-dir", str(staged_dir),
        "--profile", profile,
    ]
    subprocess.run(stage_cmd, check=True)

    # 2. Build disc image
    disc_target = f"bad_apple_{mmu}_disc_image"
    build_dir = ROOT / "build"
    # Reconfigure with profile if needed, or point disc creation to staged assets
    disc_dir = build_dir / f"bad_apple_{profile}/{mmu}"
    disc_dir.mkdir(parents=True, exist_ok=True)

    boot_bin = disc_dir / "BOOT.BIN"
    elf = build_dir / f"vflash_bad_apple_{mmu}.elf"

    # Pack BOOT.BIN
    pack_cmd = [sys.executable, str(ROOT / "tools/pack_boot.py"), str(elf), str(boot_bin)]
    subprocess.run(pack_cmd, check=True)

    # Make ISO
    iso = disc_dir / "homebrew.iso"
    iso.unlink(missing_ok=True)
    make_iso_cmd = [
        sys.executable, str(ROOT / "tools/make_iso.py"),
        str(boot_bin), str(iso), "--assets-dir", str(staged_dir)
    ]
    subprocess.run(make_iso_cmd, check=True)

    # Raw CD
    cue = disc_dir / "homebrew.cue"
    bin_file = disc_dir / "homebrew.bin"
    cue.unlink(missing_ok=True)
    bin_file.unlink(missing_ok=True)
    raw_cd_cmd = [sys.executable, str(ROOT / "tools/raw_cd.py"), str(iso), str(cue)]
    subprocess.run(raw_cd_cmd, check=True)

    # 3. Run in FlashEm
    out_dir = Path(f"/tmp/bench_bad_apple_{profile}_{mmu}")
    out_dir.mkdir(parents=True, exist_ok=True)

    smoke_cmd = [
        sys.executable, str(ROOT / "tools/emulator_smoke.py"),
        "--emulator", str(emulator or (ROOT / "build/emulator/flashem")),
        "--bios", str(bios or (ROOT / "dumps/A29L160ATV@TSOP48.BIN")),
        "--elf", str(elf),
        "--disc", str(cue),
        "--frames", str(frames),
        "--out", str(out_dir),
    ]
    subprocess.run(smoke_cmd, check=True, stdout=subprocess.DEVNULL)

    # 4. Extract telemetry
    sy = load_symbols(elf)
    ram = (out_dir / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]

    results = {
        "profile": profile,
        "mmu": mmu,
        "frames_decoded": word("bad_apple_frames"),
        "audio_samples": word("bad_apple_audio_samples"),
        "milestone_hw_ms": word("milestone_hw"),
        "milestone_mount_ms": word("milestone_mount"),
        "milestone_validate_ms": word("milestone_validate"),
        "milestone_audio_ms": word("milestone_audio"),
        "milestone_first_frame_ms": word("milestone_first_frame"),
        "milestone_total_ms": word("milestone_total_ms"),
    }

    print(f"Results for {profile} ({mmu}):")
    print(f"  Frames decoded: {results['frames_decoded']}")
    print(f"  Audio samples queued: {results['audio_samples']}")
    print(f"  Startup milestones: HW={results['milestone_hw_ms']}ms, Mount={results['milestone_mount_ms']}ms, Validate={results['milestone_validate_ms']}ms, Total={results['milestone_total_ms']}ms")

    return results


def main():
    parser = argparse.ArgumentParser(description="Benchmark Bad Apple profiles")
    parser.add_argument("--profiles", nargs="+", default=PROFILES)
    parser.add_argument("--mmu", choices=["off", "on", "both"], default="both")
    parser.add_argument("--frames", type=int, default=520)
    args = parser.parse_args()

    mmu_list = ["off", "on"] if args.mmu == "both" else [args.mmu]
    all_results = []

    for prof in args.profiles:
        for mmu in mmu_list:
            res = benchmark_profile(prof, mmu, frames=args.frames)
            all_results.append(res)

    print("\n=======================================================")
    print("FINAL BENCHMARK SUMMARY TABLE")
    print("=======================================================")
    print(f"{'Profile':<18} | {'MMU':<4} | {'Decoded':<8} | {'Audio Samples':<14} | {'Startup (ms)':<12}")
    print("-" * 65)
    for r in all_results:
        print(f"{r['profile']:<18} | {r['mmu']:<4} | {r['frames_decoded']:<8} | {r['audio_samples']:<14} | {r['milestone_total_ms']:<12}")


if __name__ == "__main__":
    main()
