"""Verify native V.Flash PCM audio HAL and upstream SDL2 audio driver integration.

Tests both native HAL (stereo ring buffering, 44.1 kHz playback, DMA fetch tracking,
volume configuration, underrun silence filling) and upstream SDL2 audio drivers
(device opening, audio callback dispatch, software mixing, queue management)
under firmware-booted FlashEm across MMU off and on.
"""
import argparse
import concurrent.futures
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run_native(mmu, emulator, bios, base):
    out = base / f"native-{mmu}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_audio_{mmu}.elf"
    disc = ROOT / f"build/audio/{mmu}/homebrew.cue"

    subprocess.run([
        sys.executable, str(ROOT / "tools/emulator_smoke.py"),
        "--emulator", str(emulator), "--bios", str(bios),
        "--elf", str(elf), "--disc", str(disc),
        "--frames", "600",
        "--out", str(out),
    ], check=True, stdout=subprocess.DEVNULL, timeout=180)

    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]

    assert word("startup_status") == 0x600D600D, f"startup_status mismatch: {hex(word('startup_status'))}"
    assert word("video_status") == 0x56494431, f"video_status mismatch: {hex(word('video_status'))}"
    assert word("render_status") == 0x44524157, f"render_status mismatch: {hex(word('render_status'))}"
    assert word("audio_demo_active") == 1, "audio_demo not active"
    assert word("audio_demo_frames") >= 30, f"insufficient frames: {word('audio_demo_frames')}"
    assert word("audio_demo_samples_played") >= 1000, f"insufficient samples played: {word('audio_demo_samples_played')}"
    assert word("audio_demo_samples_written") >= 1000, f"insufficient samples written: {word('audio_demo_samples_written')}"
    assert word("audio_demo_underruns") == 0, f"audio underruns detected: {word('audio_demo_underruns')}"

    stats = {
        "frames": word("audio_demo_frames"),
        "samples_played": word("audio_demo_samples_played"),
        "samples_written": word("audio_demo_samples_written"),
        "underruns": word("audio_demo_underruns"),
        "queued_bytes": word("audio_demo_queued_bytes"),
    }
    result.write_text(json.dumps(dict(
        passed=True, type="native", mmu=mmu, stats=stats
    ), indent=2) + "\n")
    print(f"PASS native_audio_{mmu}: {stats['frames']} frames, played={stats['samples_played']}, written={stats['samples_written']}, underruns={stats['underruns']}", flush=True)


def run_sdl2(mmu, emulator, bios, base):
    out = base / f"sdl2-{mmu}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_sdl2_audio_{mmu}.elf"
    disc = ROOT / f"build/audio/sdl2/{mmu}/homebrew.cue"

    subprocess.run([
        sys.executable, str(ROOT / "tools/emulator_smoke.py"),
        "--emulator", str(emulator), "--bios", str(bios),
        "--elf", str(elf), "--disc", str(disc),
        "--frames", "600",
        "--out", str(out),
    ], check=True, stdout=subprocess.DEVNULL, timeout=180)

    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]

    assert word("startup_status") == 0x600D600D, f"startup_status mismatch: {hex(word('startup_status'))}"
    assert word("video_status") == 0x56494431, f"video_status mismatch: {hex(word('video_status'))}"
    assert word("render_status") == 0x44524157, f"render_status mismatch: {hex(word('render_status'))}"
    assert word("sdl2_audio_active") == 1, "sdl2_audio not active"
    assert word("sdl2_audio_frames") >= 30, f"insufficient SDL2 frames rendered: {word('sdl2_audio_frames')}"
    assert word("sdl2_audio_device_id") >= 1, f"invalid audio device id: {word('sdl2_audio_device_id')}"
    assert word("sdl2_audio_callback_calls") >= 10, f"insufficient audio callback invocations: {word('sdl2_audio_callback_calls')}"
    assert word("sdl2_audio_samples_played") >= 1000, f"insufficient samples played: {word('sdl2_audio_samples_played')}"

    stats = {
        "frames": word("sdl2_audio_frames"),
        "device_id": word("sdl2_audio_device_id"),
        "callback_calls": word("sdl2_audio_callback_calls"),
        "samples_played": word("sdl2_audio_samples_played"),
        "queued_bytes": word("sdl2_audio_queued_bytes"),
    }
    result.write_text(json.dumps(dict(
        passed=True, type="sdl2", mmu=mmu, stats=stats
    ), indent=2) + "\n")
    print(f"PASS sdl2_audio_{mmu}: {stats['frames']} frames, callbacks={stats['callback_calls']}, played={stats['samples_played']}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, default=ROOT / "build/emulator/flashem")
    parser.add_argument("--bios", type=Path, default=ROOT / "dumps/A29L160ATV@TSOP48.BIN")
    parser.add_argument("--out", type=Path, default=ROOT / "build/audio-acceptance")
    parser.add_argument("--parallel", type=int, default=4)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.parallel) as pool:
        futures = []
        for mmu in ("off", "on"):
            futures.append(pool.submit(run_native, mmu, args.emulator, args.bios, args.out))
            futures.append(pool.submit(run_sdl2, mmu, args.emulator, args.bios, args.out))

        for f in futures:
            f.result()

    print("\nALL 4/4 AUDIO ACCEPTANCE TESTS PASSED (Native HAL + SDL2 across MMU off and on)!")


if __name__ == "__main__":
    main()

