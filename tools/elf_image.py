"""Strict, deliberately limited ELF reader for this direct-load probe."""
import struct
from pathlib import Path

RAM_START, RAM_END = 0x10000000, 0x11000000
MAX_INPUT = 64 * 1024 * 1024


def read_bounded(path):
    with Path(path).open("rb") as stream:
        data = stream.read(MAX_INPUT + 1)
    if len(data) > MAX_INPUT:
        raise ValueError("input exceeds the 64 MiB inspection limit")
    return data


def read_elf(data):
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("expected an ELF32 little-endian version-1 file")
    fields = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    kind, machine, version, entry, phoff, _, flags, ehsize, phsize, phnum, *_ = fields
    if (kind, machine, version, ehsize, phsize) != (2, 40, 1, 52, 32):
        raise ValueError("expected an executable ARM ELF with standard headers")
    if flags >> 24 != 5 or flags & 0x400:
        raise ValueError("expected ARM EABI5 without the hard-float ABI flag")
    if not phnum or phoff < ehsize or phoff + phsize * phnum > len(data):
        raise ValueError("invalid program-header range")
    segments = []
    for index in range(phnum):
        typ, off, va, pa, filesz, memsz, perms, align = struct.unpack_from("<8I", data, phoff + index * phsize)
        if typ in (2, 3):
            raise ValueError("dynamic/interpreted executables are unsupported")
        if typ != 1:
            continue
        if filesz > memsz or off + filesz > len(data):
            raise ValueError("segment file range is invalid")
        if va != pa or not RAM_START <= pa < RAM_END or memsz > RAM_END - pa:
            raise ValueError("segment must use matching physical/virtual addresses in SDRAM")
        if align > 1 and (align & (align - 1) or (pa - off) % align):
            raise ValueError("invalid segment alignment")
        if perms & 3 == 3:
            raise ValueError("writable executable segment is unexpected")
        if any(pa < s["address"] + s["memory_size"] and s["address"] < pa + memsz for s in segments):
            raise ValueError("overlapping load segments")
        segments.append(dict(address=pa, memory_size=memsz, file_size=filesz,
                             offset=off, flags=perms))
    if entry % 4 or not any(s["flags"] & 1 and s["address"] <= entry < s["address"] + s["file_size"] for s in segments):
        raise ValueError("ARM entry must be aligned and backed by an executable segment")
    return dict(entry=entry, segments=segments)
