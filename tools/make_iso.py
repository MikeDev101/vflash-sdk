"""Stage a supplied BOOT.BIN into a candidate ISO 9660 filesystem.

No claim of firmware acceptance or optical-disc bootability is made.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from elf_image import read_bounded
from inspect_boot import inspect


def build_iso(boot, output):
    data = read_bounded(boot)
    inspect(data)  # Only partial structural checks, NOT boot validation.
    output = Path(output).absolute()
    if output.exists():
        raise ValueError(f"refusing to overwrite {output}")
    tool = next((shutil.which(name) for name in ("xorriso", "genisoimage", "mkisofs") if shutil.which(name)), None)
    if tool is None:
        raise ValueError("install xorriso, genisoimage, or mkisofs")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vflash-iso-", dir=output.parent) as temp:
        temp = Path(temp)
        root = temp / "root"
        system = root / "0SYSTEM"
        system.mkdir(parents=True)
        (system / "BOOT.BIN").write_bytes(data)
        image = temp / "candidate.iso"
        command = [tool]
        if Path(tool).name == "xorriso":
            command += ["-as", "mkisofs"]
        command += ["-iso-level", "1", "-V", "VFLASH_DEV", "-o", str(image), str(root)]
        subprocess.run(command, check=True)
        # Same filesystem; exclusive publication leaves no partial target.
        os.link(image, output)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"{digest}  {output}")
    print("ISO payload image: use raw_cd.py to add sector integrity and create a BIN/CUE track.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("boot_bin", type=Path)
    parser.add_argument("output_iso", type=Path)
    args = parser.parse_args()
    try:
        build_iso(args.boot_bin, args.output_iso)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ISO generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
