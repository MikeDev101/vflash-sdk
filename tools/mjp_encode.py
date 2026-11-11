#!/usr/bin/env python3
"""Host-side MJP encoder and validator for V.Flash Video.

Produces and verifies .MJP Motion JPEG container files for V.Flash console.
Supports:
- Generating synthetic test videos with audio (no external video files needed)
- Converting arbitrary input videos using ffmpeg
- 16-bit PCM and IMA ADPCM audio encoding
- Halfword byte swapping for V.Flash hardware/software layout
- Bitstream verification and frame-by-frame decoding check
"""

import argparse
import io
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw, ImageFont

MIAV_MAGIC = 0x5641494D  # "MIAV" in little-endian uint32
HEADER_SIZE = 64

CHUNK_VIDEO = b"00dc"
CHUNK_AUDIO = b"01wb"

AUDIO_FMT_NONE = 0x0000
AUDIO_FMT_ADPCM = 0x0001
AUDIO_FMT_PCM16 = 0x0004

IMA_STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
]

IMA_INDEX_TABLE = [
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
]


def swap_bytes_16(data: bytes) -> bytes:
    """Swap adjacent pairs of bytes for V.Flash MJP video frames."""
    b = bytearray(data)
    if len(b) % 2 != 0:
        b.append(0)
    for i in range(0, len(b), 2):
        b[i], b[i + 1] = b[i + 1], b[i]
    return bytes(b)


def unswap_bytes_16(data: bytes) -> bytes:
    """Reverse byte swapping (identical operation)."""
    return swap_bytes_16(data)


def encode_ima_adpcm(samples: list) -> bytes:
    """Encode 16-bit mono PCM samples to IMA ADPCM chunk."""
    if not samples:
        return b""
    initial_sample = samples[0]
    initial_index = 0
    # Header: int16 sample, uint8 index, uint8 pad
    out = bytearray(struct.pack("<hBB", initial_sample, initial_index, 0))
    pred_sample = initial_sample
    step_index = initial_index

    # Process remaining samples in pairs
    sample_idx = 1
    while sample_idx < len(samples):
        nibbles = []
        for _ in range(2):
            if sample_idx < len(samples):
                sample = samples[sample_idx]
                sample_idx += 1
            else:
                sample = pred_sample

            diff = sample - pred_sample
            sign = 8 if diff < 0 else 0
            if sign:
                diff = -diff

            step = IMA_STEP_TABLE[step_index]
            delta = 0
            vpdiff = step >> 3

            if diff >= step:
                delta |= 4
                diff -= step
                vpdiff += step
            if diff >= (step >> 1):
                delta |= 2
                diff -= step >> 1
                vpdiff += step >> 1
            if diff >= (step >> 2):
                delta |= 1
                vpdiff += step >> 2

            if sign:
                pred_sample -= vpdiff
            else:
                pred_sample += vpdiff

            pred_sample = max(-32768, min(32767, pred_sample))
            step_index += IMA_INDEX_TABLE[delta]
            step_index = max(0, min(88, step_index))
            nibbles.append(delta | sign)

        out.append((nibbles[1] << 4) | (nibbles[0] & 0x0F))

    return bytes(out)


def decode_ima_adpcm(data: bytes) -> list:
    """Decode IMA ADPCM chunk to 16-bit PCM samples."""
    if len(data) < 4:
        return []
    initial_sample, initial_index, _ = struct.unpack_from("<hBB", data, 0)
    samples = [initial_sample]
    pred_sample = initial_sample
    step_index = max(0, min(88, initial_index))

    for byte in data[4:]:
        for shift in (0, 4):
            nibble = (byte >> shift) & 0x0F
            step = IMA_STEP_TABLE[step_index]
            diff = step >> 3
            if nibble & 4:
                diff += step
            if nibble & 2:
                diff += step >> 1
            if nibble & 1:
                diff += step >> 2
            if nibble & 8:
                pred_sample -= diff
            else:
                pred_sample += diff
            pred_sample = max(-32768, min(32767, pred_sample))
            step_index += IMA_INDEX_TABLE[nibble & 7]
            step_index = max(0, min(88, step_index))
            samples.append(pred_sample)

    return samples


def generate_synthetic_audio(num_frames: int, fps: int = 15, sample_rate: int = 22050) -> bytes:
    """Generate a clean synthetic 16-bit PCM mono audio track."""
    samples_per_frame = sample_rate // fps
    total_samples = num_frames * samples_per_frame
    samples = []
    
    # Generate pleasant chords / melody synchronized to frames
    frequencies = [261.63, 329.63, 392.00, 523.25]  # C4, E4, G4, C5
    for i in range(total_samples):
        frame_idx = i // samples_per_frame
        note_idx = (frame_idx // 4) % len(frequencies)
        freq = frequencies[note_idx]
        t = i / sample_rate
        
        # Base musical tone
        val = 0.35 * math.sin(2.0 * math.pi * freq * t)
        
        # Beat click on the start of each second
        if (i % sample_rate) < (sample_rate // 30):
            val += 0.4 * math.sin(2.0 * math.pi * 1000.0 * t)
            
        # Envelope to prevent clicks
        clamped = max(-32767, min(32767, int(val * 32767)))
        samples.append(clamped)

    return struct.pack(f"<{len(samples)}h", *samples)


def generate_synthetic_frame(frame_num: int, total_frames: int, width: int, height: int) -> Image.Image:
    """Generate a single frame of synthetic animation."""
    img = Image.new("RGB", (width, height), (20, 24, 40))
    draw = ImageDraw.Draw(img)

    # Dynamic background gradient / pattern
    t = frame_num / max(1, total_frames)
    cx = int(width * (0.5 + 0.35 * math.cos(2 * math.pi * t)))
    cy = int(height * (0.5 + 0.35 * math.sin(2 * math.pi * t)))
    r = min(width, height) // 3

    # Draw colorful geometric circles
    colors = [
        (220, 50, 50),
        (50, 200, 80),
        (50, 100, 240),
        (240, 200, 40)
    ]
    color = colors[frame_num % len(colors)]
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color, outline=(255, 255, 255))

    # Center bounding box
    margin = 8
    draw.rectangle([margin, margin, width - margin, height - margin], outline=(100, 150, 255), width=2)

    # Telemetry and text
    title = "V.FLASH MJP"
    draw.text((margin + 6, margin + 4), title, fill=(255, 255, 255))
    info = f"F:{frame_num+1:03d}/{total_frames:03d}"
    draw.text((margin + 6, height - margin - 14), info, fill=(200, 240, 255))
    
    # Progress bar
    bar_w = width - margin * 2 - 12
    prog_w = int(bar_w * ((frame_num + 1) / total_frames))
    draw.rectangle([margin + 6, height - margin - 4, margin + 6 + bar_w, height - margin - 2], fill=(50, 50, 70))
    draw.rectangle([margin + 6, height - margin - 4, margin + 6 + prog_w, height - margin - 2], fill=(80, 220, 120))

    return img


def encode_mjp(frames_jpeg: list, audio_bytes: bytes, width: int, height: int,
               fps: int = 15, audio_fmt: int = AUDIO_FMT_PCM16) -> bytes:
    """Encode video frames and audio into an MJP container."""
    frame_count = len(frames_jpeg)
    samples_per_frame = (22050 // fps) if audio_fmt != AUDIO_FMT_NONE else 0
    bytes_per_sample = 2  # 16-bit mono

    chunks = bytearray()
    audio_offset = 0

    for frame_idx, frame_data in enumerate(frames_jpeg):
        # 1. Video chunk '00dc'
        swapped_video = swap_bytes_16(frame_data)
        video_size = len(swapped_video)
        chunks.extend(b"00dc")
        chunks.extend(struct.pack("<II", frame_idx, video_size))
        chunks.extend(swapped_video)
        if video_size % 2 != 0:
            chunks.append(0)  # pad

        # 2. Audio chunk '01wb' (if audio present)
        if audio_fmt != AUDIO_FMT_NONE and audio_bytes:
            req_bytes = samples_per_frame * bytes_per_sample
            pcm_chunk = audio_bytes[audio_offset:audio_offset + req_bytes]
            audio_offset += req_bytes
            if len(pcm_chunk) < req_bytes:
                pcm_chunk = pcm_chunk + b"\x00" * (req_bytes - len(pcm_chunk))

            if audio_fmt == AUDIO_FMT_PCM16:
                payload = pcm_chunk
            elif audio_fmt == AUDIO_FMT_ADPCM:
                samples = list(struct.unpack(f"<{len(pcm_chunk)//2}h", pcm_chunk))
                payload = encode_ima_adpcm(samples)
            else:
                payload = b""

            audio_size = len(payload)
            chunks.extend(b"01wb")
            chunks.extend(struct.pack("<II", frame_idx * samples_per_frame, audio_size))
            chunks.extend(payload)
            if audio_size % 2 != 0:
                chunks.append(0)  # pad

    # 64-byte MIAV header
    payload_size = len(chunks)
    header = bytearray(64)
    struct.pack_into("<I", header, 0, MIAV_MAGIC)
    struct.pack_into("<I", header, 4, HEADER_SIZE)
    struct.pack_into("<I", header, 8, payload_size)
    struct.pack_into("<I", header, 12, 0)  # reserved
    struct.pack_into("<I", header, 16, frame_count)
    struct.pack_into("<I", header, 20, fps)
    struct.pack_into("<I", header, 24, audio_fmt)
    struct.pack_into("<I", header, 28, width)
    struct.pack_into("<I", header, 32, height)
    # Remaining 28 bytes are zero

    return bytes(header + chunks)


def verify_mjp(data: bytes) -> dict:
    """Verify an MJP file and decode each frame to test integrity."""
    if len(data) < HEADER_SIZE:
        raise ValueError(f"File too small: {len(data)} bytes")

    magic, hdr_size, payload_size, _, frame_count, fps, audio_fmt, width, height = struct.unpack_from(
        "<IIIIIIIII", data, 0
    )
    if magic != MIAV_MAGIC:
        raise ValueError(f"Invalid magic: {hex(magic)} (expected {hex(MIAV_MAGIC)})")
    if hdr_size != HEADER_SIZE:
        raise ValueError(f"Invalid header size: {hdr_size}")
    if len(data) < HEADER_SIZE + payload_size:
        raise ValueError(f"File truncated: expected {HEADER_SIZE + payload_size}, got {len(data)}")

    pos = HEADER_SIZE
    end = HEADER_SIZE + payload_size
    video_frames_read = 0
    audio_chunks_read = 0
    total_audio_bytes = 0

    while pos + 12 <= end:
        chunk_id = data[pos:pos + 4]
        timestamp, chunk_size = struct.unpack_from("<II", data, pos + 4)
        pos += 12

        payload = data[pos:pos + chunk_size]
        pos += chunk_size
        if chunk_size % 2 != 0:
            pos += 1  # pad

        if chunk_id == CHUNK_VIDEO:
            # Unswap bytes and verify valid JPEG
            jpeg_data = unswap_bytes_16(payload)
            if not jpeg_data.startswith(b"\xFF\xD8"):
                raise ValueError(f"Frame {video_frames_read}: missing JPEG SOI marker")
            try:
                img = Image.open(io.BytesIO(jpeg_data))
                img.load()
                if img.size != (width, height):
                    raise ValueError(f"Frame {video_frames_read}: size mismatch {img.size} vs ({width},{height})")
            except Exception as e:
                raise ValueError(f"Frame {video_frames_read}: JPEG decode error: {e}")
            video_frames_read += 1

        elif chunk_id == CHUNK_AUDIO:
            audio_chunks_read += 1
            total_audio_bytes += chunk_size
            if audio_fmt == AUDIO_FMT_ADPCM:
                samples = decode_ima_adpcm(payload)
                if not samples:
                    raise ValueError(f"Audio chunk {audio_chunks_read}: failed to decode ADPCM")

    if video_frames_read != frame_count:
        raise ValueError(f"Frame count mismatch: expected {frame_count}, read {video_frames_read}")

    return {
        "magic": "MIAV",
        "frame_count": frame_count,
        "fps": fps,
        "width": width,
        "height": height,
        "audio_fmt": "ADPCM" if audio_fmt == AUDIO_FMT_ADPCM else ("PCM16" if audio_fmt == AUDIO_FMT_PCM16 else "NONE"),
        "audio_chunks": audio_chunks_read,
        "total_audio_bytes": total_audio_bytes,
        "file_size": len(data)
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--synthetic", type=int, default=0, help="generate synthetic video with N frames")
    parser.add_argument("--input", type=Path, default=None, help="input video or image file to convert")
    parser.add_argument("--audio", type=Path, default=None, help="input audio WAV file")
    parser.add_argument("--output", type=Path, required=True, help="output .MJP file path")
    parser.add_argument("--width", type=int, default=224, help="video width (default 224)")
    parser.add_argument("--height", type=int, default=112, help="video height (default 112)")
    parser.add_argument("--fps", type=int, default=15, help="video fps (default 15)")
    parser.add_argument("--audio-format", choices=["pcm", "adpcm", "none"], default="pcm",
                        help="audio encoding format")
    parser.add_argument("--quality", type=int, default=75, help="JPEG quality (default 75)")
    parser.add_argument("--verify", action="store_true", help="verify output file after encoding")

    args = parser.parse_args()

    audio_fmt_map = {
        "pcm": AUDIO_FMT_PCM16,
        "adpcm": AUDIO_FMT_ADPCM,
        "none": AUDIO_FMT_NONE
    }
    audio_fmt = audio_fmt_map[args.audio_format]

    frames_jpeg = []
    audio_bytes = b""

    if args.synthetic > 0:
        print(f"Generating synthetic video: {args.synthetic} frames @ {args.width}x{args.height}, {args.fps} fps...")
        for i in range(args.synthetic):
            img = generate_synthetic_frame(i, args.synthetic, args.width, args.height)
            bio = io.BytesIO()
            img.save(bio, format="JPEG", quality=args.quality, subsampling="4:2:0")
            frames_jpeg.append(bio.getvalue())

        if audio_fmt != AUDIO_FMT_NONE:
            print("Generating synchronized audio...")
            audio_bytes = generate_synthetic_audio(args.synthetic, args.fps, 22050)

    elif args.input:
        print(f"Converting video {args.input} with ffmpeg...")
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            frames_dir = tmp_path / "frames"
            frames_dir.mkdir()
            wav_path = tmp_path / "audio.wav"

            # 1. Extract frames at requested resolution and fps
            cmd_video = [
                "ffmpeg", "-y", "-i", str(args.input),
                "-vf", f"scale={args.width}:{args.height},fps={args.fps}",
                "-q:v", str(max(1, 31 - (args.quality * 30 // 100))),
                str(frames_dir / "frame_%05d.jpg")
            ]
            subprocess.run(cmd_video, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            for f in sorted(frames_dir.glob("*.jpg")):
                frames_jpeg.append(f.read_bytes())

            # 2. Extract audio if requested
            if audio_fmt != AUDIO_FMT_NONE:
                cmd_audio = [
                    "ffmpeg", "-y", "-i", str(args.input),
                    "-ar", "22050", "-ac", "1", "-f", "s16le",
                    str(tmp_path / "audio.pcm")
                ]
                try:
                    subprocess.run(cmd_audio, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    audio_bytes = (tmp_path / "audio.pcm").read_bytes()
                except subprocess.CalledProcessError:
                    print("Warning: Failed to extract audio or input has no audio track.")
                    audio_fmt = AUDIO_FMT_NONE
    else:
        parser.error("Must specify either --synthetic <N> or --input <video_file>")

    print(f"Encoding MJP: {len(frames_jpeg)} frames, {len(audio_bytes)} audio bytes...")
    mjp_data = encode_mjp(frames_jpeg, audio_bytes, args.width, args.height, args.fps, audio_fmt)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(mjp_data)
    print(f"Saved: {args.output} ({len(mjp_data)} bytes)")

    if args.verify or True:  # always verify during creation
        info = verify_mjp(mjp_data)
        print(f"Verification PASSED: {info}")

    return 0


if __name__ == "__main__":
    sys.exit(main())

