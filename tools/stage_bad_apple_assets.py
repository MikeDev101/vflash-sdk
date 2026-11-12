#!/usr/bin/env python3
"""Stage Bad Apple video assets into the disc assets staging directory.

Maps long profile names to ISO 9660 Level 1 compliant 8.3 filenames.
If no local media is found, falls back to synthetic disc.mjp for CI.
"""

import argparse
from pathlib import Path
import shutil
import sys

PROFILES = [
    ("320x240_10fps", "BA320_10.MJP"),
    ("320x240_15fps", "BA320_15.MJP"),
    ("256x192_15fps", "BA256_15.MJP"),
    ("256x192_20fps", "BA256_20.MJP"),
    ("240x160_15fps", "BA240_15.MJP"),
    ("240x160_20fps", "BA240_20.MJP"),
    ("240x160_24fps", "BA240_24.MJP"),
    ("192x144_24fps", "BA192_24.MJP"),
    ("192x144_30fps", "BA192_30.MJP"),
    ("160x120_24fps", "BA160_24.MJP"),
    ("160x120_30fps", "BA160_30.MJP"),
]


def stage_assets(src_dir: Path, dst_dir: Path, selected_profile: str, fallback_file: Path, all_profiles: bool = False):
    shutil.rmtree(dst_dir, ignore_errors=True)
    dst_dir.mkdir(parents=True, exist_ok=True)
    staged = 0

    # 1. Check for primary selected profile
    primary_src = src_dir / f"bad_apple_{selected_profile}.mjp"
    if primary_src.is_file():
        shutil.copy2(primary_src, dst_dir / "DISC.MJP")
        print(f"[stage_bad_apple] Staged primary profile '{selected_profile}' -> DISC.MJP ({primary_src.stat().st_size} bytes)")
        staged += 1
    elif fallback_file and fallback_file.is_file():
        shutil.copy2(fallback_file, dst_dir / "DISC.MJP")
        print(f"[stage_bad_apple] Note: {primary_src} not found. Using synthetic fallback -> DISC.MJP")
        staged += 1
    else:
        raise FileNotFoundError(f"Neither {primary_src} nor fallback {fallback_file} exists")

    # 2. Stage secondary profiles
    if all_profiles:
        for prof_name, short_name in PROFILES:
            candidate = src_dir / f"bad_apple_{prof_name}.mjp"
            if candidate.is_file():
                dst = dst_dir / short_name
                shutil.copy2(candidate, dst)
                staged += 1
    else:
        # Stage 2 alternative profiles for testing in-engine profile switching
        for prof_name, short_name in [("160x120_30fps", "BA160_30.MJP"), ("320x240_10fps", "BA320_10.MJP")]:
            candidate = src_dir / f"bad_apple_{prof_name}.mjp"
            if candidate.is_file() and prof_name != selected_profile:
                shutil.copy2(candidate, dst_dir / short_name)
                staged += 1

    print(f"[stage_bad_apple] Successfully staged {staged} video assets to {dst_dir}")


def main():
    parser = argparse.ArgumentParser(description="Stage Bad Apple assets for disc creation")
    parser.add_argument("--src-dir", type=Path, default=Path("assets/local/bad-apple"))
    parser.add_argument("--dst-dir", type=Path, required=True)
    parser.add_argument("--profile", default="256x192_15fps")
    parser.add_argument("--fallback", type=Path, default=Path("examples/fmv/assets/disc.mjp"))
    parser.add_argument("--all-profiles", action="store_true", help="Stage all 11 profiles")
    args = parser.parse_args()

    stage_assets(args.src_dir, args.dst_dir, args.profile, args.fallback, args.all_profiles)


if __name__ == "__main__":
    main()
