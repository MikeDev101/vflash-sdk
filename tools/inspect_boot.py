"""Report evidenced BOOT header fields and bounded signature observations.

This is not a full chunk parser or firmware acceptance validator.
"""
import argparse
import hashlib
import json
import struct
import sys
from elf_image import RAM_START, RAM_END, read_bounded


def inspect(data):
    if len(data) < 0x14:
        raise ValueError("truncated BOOT header (need at least 0x14 bytes)")
    if data[:4] != b"BOOT":
        raise ValueError("missing BOOT signature at offset zero")
    size_word, load, entry_offset = struct.unpack_from("<3I", data, 4)
    if entry_offset % 4 or entry_offset + 4 > len(data):
        raise ValueError("entry offset is unaligned or outside the file")
    instruction = struct.unpack_from("<I", data, entry_offset)[0]
    warnings = ["Partial inspection only; firmware acceptance is unverified."]
    if not RAM_START <= load < RAM_END or len(data) > RAM_END - load:
        warnings.append("Whole-file load does not fit FlashEm's SDRAM map.")
    if load % 4:
        warnings.append("Load address is not word aligned.")
    if size_word != len(data):
        warnings.append("Image-size word differs from file length for the inspected ROM's format.")
    result = dict(file_size=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  image_size=size_word, load_address=load,
                  entry_offset=entry_offset, entry_stub_address=load + entry_offset,
                  entry_instruction=instruction,
                  signature_hits={}, warnings=warnings)
    # AL LDR pc,[pc,+/-imm12], pre-indexed, no writeback; this is code,
    # not an invented entry-address header field.
    if instruction & 0xFF7FF000 == 0xE51FF000:
        delta = instruction & 0xFFF
        literal = entry_offset + 8 + (delta if instruction & (1 << 23) else -delta)
        if literal < 0 or literal + 4 > len(data) or literal % 4:
            raise ValueError("entry LDR literal is unaligned or outside the file")
        result["entry_literal_offset"] = literal
        result["entry_literal_value"] = struct.unpack_from("<I", data, literal)[0]
    else:
        warnings.append("Entry word is not the supported PC-relative LDR pattern; no target inferred.")
    for signature in (b"BOOT", b"REL\0", b"DBG\0"):
        hits, start = [], 0
        while len(hits) < 32:
            found = data.find(signature, start)
            if found < 0:
                break
            hits.append(found)
            start = found + 1
        result["signature_hits"][signature.decode("ascii")] = dict(offsets=hits, capped=len(hits) == 32)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("boot_bin")
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(read_bounded(args.boot_bin)), indent=2))
    except (ValueError, OSError) as error:
        print(f"BOOT inspection failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
