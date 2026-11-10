"""Pack a fixed-address ELF for the inspected ROM's simple disc loader.

Only a self-contained executable at 0x10C00018 is supported. No proprietary
template, relocation/debug chunks, or firmware bytes are included.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
from elf_image import RAM_END, read_bounded, read_elf

LOAD_BASE = 0x10C00000
PAYLOAD_BASE = LOAD_BASE + 24


def pack_elf(data):
    elf = read_elf(data)
    spans = [s for s in elf["segments"] if s["file_size"]]
    if min(s["address"] for s in spans) != PAYLOAD_BASE:
        raise ValueError("link for firmware-disc.ld: first payload byte must be at 0x10C00018")
    if any(s["address"] < PAYLOAD_BASE or s["address"] + s["memory_size"] > RAM_END - 16 * 1024
           for s in elf["segments"]):
        raise ValueError("ELF exceeds the probe's supported payload/stack region")
    end = max(s["address"] + s["file_size"] for s in spans)
    size = (end - LOAD_BASE + 511) & ~511
    if size > RAM_END - 16 * 1024 - LOAD_BASE:
        raise ValueError("sector-padded BOOT image would overlap the reserved stack")
    result = bytearray(size)
    struct.pack_into("<4s5I", result, 0, b"BOOT", size, LOAD_BASE, 16,
                     0xE51FF004, elf["entry"])  # LDR pc,[pc,#-4]; entry literal
    for segment in spans:
        start = segment["address"] - LOAD_BASE
        offset, length = segment["offset"], segment["file_size"]
        result[start:start + length] = data[offset:offset + length]
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    try:
        result = pack_elf(read_bounded(args.elf))
        with args.output.open("xb") as output:
            output.write(result)
        if args.verbose:
            print(f"{len(result)} bytes; load 0x{LOAD_BASE:08X}; entry stub +0x10; 512-byte padding")
        print(f"{hashlib.sha256(result).hexdigest()}  {args.output}")
    except (OSError, ValueError) as error:
        print(f"BOOT packaging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
