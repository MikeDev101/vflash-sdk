"""Encode ISO payload sectors as a single Mode 1/2352 BIN/CUE track.

Uses the CD-ROM EDC polynomial and two GF(256) parity passes. Parameters are
cross-checked against Neill Corlett's ECM and independently supplied sectors;
see docs/firmware.md. No subchannel/audio/multitrack authoring is attempted.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys
import tempfile


def twice(value):
    return ((value << 1) ^ (0x11D if value & 0x80 else 0)) & 0xFF


DOUBLE = tuple(twice(i) for i in range(256))
DIVIDE_THREE = {i ^ DOUBLE[i]: i for i in range(256)}


def edc(data):
    # Reflected CD-ROM polynomial; initial and final xor are both zero.
    state = 0
    for byte in data:
        state ^= byte
        for _ in range(8):
            state = (state >> 1) ^ (0xD8018001 if state & 1 else 0)
    return state


def parity_pass(data, lanes, symbols, stride, step):
    """Solve the two parity symbols for each interleaved RS codeword."""
    result = bytearray(2 * lanes)
    size = lanes * symbols
    for lane in range(lanes):
        first = (lane // 2) * stride + lane % 2
        total, weighted = 0, 0
        for position in range(symbols):
            value = data[(first + position * step) % size]
            total ^= value
            weighted = DOUBLE[weighted ^ value]
        left = DIVIDE_THREE[DOUBLE[weighted] ^ total]
        result[lane] = left
        result[lane + lanes] = left ^ total
    return result


def encode_sector(payload, lba):
    if len(payload) != 2048 or not 0 <= lba < 449850:
        raise ValueError("expected 2048-byte payload and an LBA below 99:59:74")
    absolute = lba + 150
    minute, second_frame = divmod(absolute, 60 * 75)
    second, frame = divmod(second_frame, 75)
    bcd = lambda number: (number // 10) * 16 + number % 10
    sector = bytearray(2352)
    sector[:12] = b"\0" + b"\xff" * 10 + b"\0"
    sector[12:16] = bytes((bcd(minute), bcd(second), bcd(frame), 1))
    sector[16:2064] = payload
    struct.pack_into("<I", sector, 2064, edc(sector[:2064]))
    sector[2076:2248] = parity_pass(sector[12:2076], 86, 24, 2, 86)
    sector[2248:2352] = parity_pass(sector[12:2248], 52, 43, 86, 88)
    return bytes(sector)


def convert(iso, cue):
    iso, cue = Path(iso), Path(cue).absolute()
    if cue.suffix.lower() != ".cue" or any(ch in cue.stem for ch in ('"', '\n', '\r')):
        raise ValueError("output must be a .cue path with a safe quoted filename")
    binary = cue.with_suffix(".bin")
    if cue.exists() or binary.exists():
        raise ValueError("refusing to overwrite BIN/CUE output")
    size = iso.stat().st_size
    if not size or size % 2048 or size // 2048 > 449850:
        raise ValueError("ISO must be a nonempty sequence of supported 2048-byte sectors")
    cue.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="raw-cd-", dir=cue.parent) as temp:
        temp = Path(temp)
        digest = hashlib.sha256()
        with iso.open("rb") as source, (temp / "track.bin").open("wb") as output:
            for lba in range(size // 2048):
                sector = encode_sector(source.read(2048), lba)
                digest.update(sector)
                output.write(sector)
            if source.read(1):
                raise ValueError("input ISO changed during encoding")
        (temp / "track.cue").write_text(
            f'FILE "{binary.name}" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n', encoding="utf-8")
        os.link(temp / "track.bin", binary)
        try:
            os.link(temp / "track.cue", cue)
        except OSError:
            binary.unlink()
            raise
    print(f"{digest.hexdigest()}  {binary}")
    print(f"{size // 2048} Mode 1 sectors; CUE: {cue}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("cue", type=Path)
    args = parser.parse_args()
    try:
        convert(args.iso, args.cue)
    except (OSError, ValueError) as error:
        print(f"Raw CD generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
