#!/usr/bin/env python3
"""Preprocesses Bad Apple!! video source into multiple resolution/framerate variants
and encodes MJP containers for V.Flash hardware benchmarking.

All generated files are strictly written to git-ignored directories.
"""

import argparse
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

# Import MJP encoding and validation functions from existing tool
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from mjp_encode import (
    AUDIO_FMT_ADPCM,
    AUDIO_FMT_PCM16,
    encode_mjp,
    encode_ima_adpcm,
    verify_mjp,
)

VARIANTS = [
    (320, 240, 10),
    (320, 240, 15),
    (256, 192, 15),
    (256, 192, 20),
    (240, 160, 15),
    (240, 160, 20),
    (240, 160, 24),
    (192, 144, 24),
    (192, 144, 30),
    (160, 120, 24),
    (160, 120, 30),
]


def probe_media(source: Path) -> dict:
    cmd = [
        "ffprobe", "-v", "error",
        "-show_format", "-show_streams",
        "-of", "json",
        str(source)
    ]
    res = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return json.loads(res.stdout)


def extract_audio(source: Path, out_dir: Path, clip_duration: float = None):
    wav_path = out_dir / "audio_22050Hz_mono.wav"
    pcm_path = out_dir / "audio_22050Hz_mono.pcm"
    adpcm_path = out_dir / "audio_22050Hz_mono.adpcm"

    cmd = ["ffmpeg", "-y"]
    if clip_duration:
        cmd += ["-t", str(clip_duration)]
    cmd += [
        "-i", str(source),
        "-vn", "-ar", "22050", "-ac", "1",
        "-c:a", "pcm_s16le",
        str(wav_path)
    ]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Extract raw s16le PCM bytes
    cmd_pcm = ["ffmpeg", "-y"]
    if clip_duration:
        cmd_pcm += ["-t", str(clip_duration)]
    cmd_pcm += [
        "-i", str(source),
        "-vn", "-ar", "22050", "-ac", "1",
        "-f", "s16le",
        str(pcm_path)
    ]
    subprocess.run(cmd_pcm, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Encode IMA ADPCM
    pcm_bytes = pcm_path.read_bytes()
    num_samples = len(pcm_bytes) // 2
    samples = list(struct.unpack(f"<{num_samples}h", pcm_bytes))
    adpcm_bytes = encode_ima_adpcm(samples)
    adpcm_path.write_bytes(adpcm_bytes)

    return wav_path, pcm_path, adpcm_path, pcm_bytes


def generate_variant_video(source: Path, width: int, height: int, fps: int,
                           out_dir: Path, clip_duration: float = None):
    """Generate both grayscale and strictly monochrome MP4 test streams."""
    base_name = f"bad_apple_{width}x{height}_{fps}fps"
    gray_mp4 = out_dir / f"{base_name}_gray.mp4"
    mono_mp4 = out_dir / f"{base_name}_mono.mp4"

    # Scale with letterbox padding to preserve 4:3 aspect ratio
    scale_pad = (
        f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2,setsar=1"
    )

    # 1. Grayscale variant
    cmd_gray = ["ffmpeg", "-y"]
    if clip_duration:
        cmd_gray += ["-t", str(clip_duration)]
    cmd_gray += [
        "-i", str(source),
        "-vf", f"{scale_pad},fps={fps},format=gray",
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p",
        "-an", str(gray_mp4)
    ]
    subprocess.run(cmd_gray, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # 2. Strictly monochrome 1-bit thresholded variant
    cmd_mono = ["ffmpeg", "-y"]
    if clip_duration:
        cmd_mono += ["-t", str(clip_duration)]
    cmd_mono += [
        "-i", str(source),
        "-vf", f"{scale_pad},fps={fps},format=gray,curves=all='0/0 0.49/0 0.5/1 1/1'",
        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p",
        "-an", str(mono_mp4)
    ]
    subprocess.run(cmd_mono, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    return gray_mp4, mono_mp4


def encode_mjp_variant(source: Path, width: int, height: int, fps: int,
                       audio_pcm: bytes, out_dir: Path, quality: int = 60,
                       clip_duration: float = None) -> Path:
    """Encode compliant .MJP container with JPEG video and interleaved IMA ADPCM."""
    out_mjp = out_dir / f"bad_apple_{width}x{height}_{fps}fps.mjp"

    scale_pad = (
        f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2,setsar=1"
    )

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir)
        frames_dir = tmp_path / "frames"
        frames_dir.mkdir()

        cmd = ["ffmpeg", "-y"]
        if clip_duration:
            cmd += ["-t", str(clip_duration)]
        cmd += [
            "-i", str(source),
            "-vf", f"{scale_pad},fps={fps}",
            "-q:v", str(max(1, 31 - (quality * 30 // 100))),
            str(frames_dir / "frame_%05d.jpg")
        ]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        frames_jpeg = []
        for f in sorted(frames_dir.glob("*.jpg")):
            frames_jpeg.append(f.read_bytes())

        # If clip_duration is active, slice audio bytes to match video duration
        expected_audio_bytes = int(len(frames_jpeg) * (22050 * 2) / fps)
        audio_slice = audio_pcm[:expected_audio_bytes]

        mjp_data = encode_mjp(frames_jpeg, audio_slice, width, height, fps, AUDIO_FMT_ADPCM)
        out_mjp.write_bytes(mjp_data)

    return out_mjp


def main():
    parser = argparse.ArgumentParser(description="Preprocess Bad Apple!! for V.Flash benchmarks")
    parser.add_argument("--source", type=Path, default=ROOT / "assets/local/bad-apple/source.mp4",
                        help="Path to downloaded source video")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "assets/local/bad-apple",
                        help="Output directory (must be git-ignored)")
    parser.add_argument("--clip", type=float, default=None,
                        help="Optional clip duration in seconds (for fast verification)")
    parser.add_argument("--quality", type=int, default=60,
                        help="JPEG quality for MJP encoding (default 60)")
    parser.add_argument("--only", type=str, default=None,
                        help="Filter specific variant e.g. '320x240@15' or '160x120@30'")
    parser.add_argument("--skip-mjp", action="store_true",
                        help="Skip MJP container encoding")
    parser.add_argument("--verify", action="store_true", default=True,
                        help="Verify generated MJP containers")
    args = parser.parse_args()

    if not args.source.exists():
        print(f"Error: Source video not found: {args.source}", file=sys.stderr)
        sys.exit(1)

    args.out_dir.mkdir(parents=True, exist_ok=True)

    print(f"=== Probing Source: {args.source} ===")
    info = probe_media(args.source)
    fmt = info.get("format", {})
    video_stream = next((s for s in info.get("streams", []) if s.get("codec_type") == "video"), {})
    audio_stream = next((s for s in info.get("streams", []) if s.get("codec_type") == "audio"), {})

    dur = float(fmt.get("duration", 0))
    print(f"Container: {fmt.get('format_name')} ({dur:.2f}s, {int(fmt.get('size', 0)):,} bytes)")
    print(f"Video: {video_stream.get('codec_name')} {video_stream.get('width')}x{video_stream.get('height')} @ {video_stream.get('r_frame_rate')} fps")
    print(f"Audio: {audio_stream.get('codec_name')} {audio_stream.get('sample_rate')} Hz, {audio_stream.get('channels')} channels")

    # 1. Extract Audio
    print("\n=== Extracting Audio Tracks ===")
    wav, pcm, adpcm, pcm_bytes = extract_audio(args.source, args.out_dir, clip_duration=args.clip)
    print(f"PCM WAV:   {wav.name} ({wav.stat().st_size:,} bytes)")
    print(f"Raw S16LE: {pcm.name} ({pcm.stat().st_size:,} bytes)")
    print(f"IMA ADPCM: {adpcm.name} ({adpcm.stat().st_size:,} bytes)")

    # 2. Process Video Variants
    target_variants = VARIANTS
    if args.only:
        ow, oh, ofps = args.only.replace("@", "x").split("x")
        target_variants = [(int(ow), int(oh), int(ofps))]

    results = []
    print("\n=== Generating Resolution / Framerate Variants ===")
    for w, h, fps in target_variants:
        print(f"\nVariant {w}x{h} @ {fps} fps...")
        gray, mono = generate_variant_video(args.source, w, h, fps, args.out_dir, clip_duration=args.clip)
        print(f"  -> Grayscale:   {gray.name} ({gray.stat().st_size:,} bytes)")
        print(f"  -> Monochrome:  {mono.name} ({mono.stat().st_size:,} bytes)")

        mjp_size = 0
        if not args.skip_mjp:
            mjp_path = encode_mjp_variant(args.source, w, h, fps, pcm_bytes, args.out_dir,
                                          quality=args.quality, clip_duration=args.clip)
            mjp_size = mjp_path.stat().st_size
            print(f"  -> MJP Stream:  {mjp_path.name} ({mjp_size:,} bytes)")

            if args.verify:
                stat = verify_mjp(mjp_path.read_bytes())
                print(f"     Verification: {stat['frame_count']} frames, {stat['audio_chunks']} chunks, {stat['file_size']:,} bytes OK")

        results.append({
            "variant": f"{w}x{h}@{fps}",
            "width": w, "height": h, "fps": fps,
            "gray_bytes": gray.stat().st_size,
            "mono_bytes": mono.stat().st_size,
            "mjp_bytes": mjp_size,
        })

    # Summary Table
    print("\n" + "=" * 70)
    print(f"{'Variant':<14} | {'Grayscale MP4':<15} | {'Monochrome MP4':<15} | {'MJP Container':<15}")
    print("-" * 70)
    total_bytes = wav.stat().st_size + pcm.stat().st_size + adpcm.stat().st_size
    for r in results:
        g_kb = f"{r['gray_bytes'] / 1024:.1f} KB"
        m_kb = f"{r['mono_bytes'] / 1024:.1f} KB"
        j_mb = f"{r['mjp_bytes'] / (1024 * 1024):.2f} MB" if r['mjp_bytes'] else "N/A"
        print(f"{r['variant']:<14} | {g_kb:<15} | {m_kb:<15} | {j_mb:<15}")
        total_bytes += r["gray_bytes"] + r["mono_bytes"] + r["mjp_bytes"]
    print("-" * 70)
    print(f"Total Local Media Cache: {total_bytes / (1024 * 1024):.2f} MB")
    print("=" * 70)


if __name__ == "__main__":
    main()
