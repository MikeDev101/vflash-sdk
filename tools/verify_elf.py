"""Check the probe's load contract and GCC's emitted architecture attributes."""
import argparse
import json
import subprocess
import sys
from elf_image import read_bounded, read_elf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("--readelf", default="arm-none-eabi-readelf")
    parser.add_argument("--objdump", default=None)
    parser.add_argument("--dis-out", default=None)
    parser.add_argument("--readelf-out", default=None)
    args = parser.parse_args()
    try:
        result = read_elf(read_bounded(args.elf))
        if args.objdump and args.dis_out:
            dis = subprocess.run([args.objdump, "-d", "-S", args.elf], check=True, stdout=subprocess.PIPE).stdout
            with open(args.dis_out, "wb") as stream:
                stream.write(dis)
        if args.readelf_out:
            report = subprocess.run([args.readelf, "-h", "-l", "-S", "-A", "-s", args.elf],
                                    check=True, stdout=subprocess.PIPE).stdout
            with open(args.readelf_out, "wb") as stream:
                stream.write(report)
            attributes = report.decode("utf-8", errors="replace")
        else:
            attributes = subprocess.run([args.readelf, "-A", args.elf], check=True,
                                        capture_output=True, text=True).stdout
        if "Tag_CPU_arch: v5TEJ" not in attributes or "Tag_ARM_ISA_use: Yes" not in attributes:
            raise ValueError("missing ARMv5TEJ / ARM-state build attributes")
        if "Tag_FP_arch:" in attributes or "Tag_Advanced_SIMD_arch:" in attributes or "Tag_ABI_VFP_args:" in attributes:
            raise ValueError("unexpected floating-point/SIMD architecture or VFP calling convention")
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"ELF verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
