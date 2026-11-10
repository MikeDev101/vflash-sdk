"""Prove probe execution by checking its RAM symbols.

The default ROM is our own four-byte ARM loop, only to satisfy FlashEm's
creation API. --load bypasses it. Supply both --bios and --disc instead to
test a real firmware-driven disc boot, with no direct program loading.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from elf_image import RAM_START, RAM_END, read_bounded, read_elf

EXPECTED = {"boot_marker": 0x56464C53, "startup_status": 0x600D600D, "data_cookie": 0x13579BDF}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--bios", type=Path, help="local original ROM; requires --disc")
    parser.add_argument("--disc", type=Path, help="homebrew CUE image; requires --bios")
    parser.add_argument("--frames", type=int, help="exit frame (default: 3 direct, 60 direct video, 1200 disc)")
    parser.add_argument("--timeout", type=float, help="host timeout in seconds (default: 30 direct, 120 disc)")
    parser.add_argument("--verify-video", action="store_true",
                        help="compare video status, RAM framebuffer and actual scanout against the host C renderer")
    parser.add_argument("--verify-animation", action="store_true",
                        help="verify animated firmware boot, buffer flips, movement, timing and exact pixels")
    args = parser.parse_args()
    if bool(args.bios) != bool(args.disc):
        parser.error("--bios and --disc must be supplied together")
    if args.verify_animation and (not args.disc or args.verify_video):
        parser.error("--verify-animation requires firmware/disc mode and replaces --verify-video")
    frames = args.frames if args.frames is not None else (1200 if args.disc else (60 if args.verify_video else 3))
    timeout = args.timeout if args.timeout is not None else (120 if args.disc else 30)
    if not 2 <= frames <= 10000 or not 0 < timeout <= 600:
        parser.error("use 2..10000 frames and a host timeout of 0..600 seconds")
    if args.verify_animation and frames < 180:
        parser.error("animation verification requires at least 180 frames")
    try:
        args.out.mkdir(parents=True, exist_ok=True)
        output = args.out.resolve()
        # Do not allow stale evidence to survive a failed run.
        for name in ("result.json", "ram.bin", "ram.bin.sram", "run.log", "frame.ppm", "frame.bgr555"):
            (output / name).unlink(missing_ok=True)
        elf = args.elf.resolve(strict=True)
        emulator = args.emulator.resolve(strict=True)
        image = read_elf(read_bounded(elf))
        bios = args.bios.resolve(strict=True) if args.bios else None
        disc = args.disc.resolve(strict=True) if args.disc else None
        if bios and bios.stat().st_size != 2 * 1024 * 1024:
            raise ValueError("firmware boot test requires a 2 MiB ROM")
        nm = subprocess.run([args.nm, "--defined-only", str(elf)], check=True,
                            capture_output=True, text=True, timeout=10).stdout
        expected = dict(EXPECTED)
        if args.verify_video or args.verify_animation:
            expected.update(video_status=0x56494431, render_status=0x44524157)
        watched = set(expected) | ({"video_pixels"} if args.verify_video or args.verify_animation else set())
        if args.verify_animation:
            watched |= {"video_back_pixels", "animation_snapshot"}
        symbols = {}
        for line in nm.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[2] in watched:
                address = int(parts[0], 16)
                if not RAM_START <= address <= RAM_END - 4:
                    raise ValueError("probe symbol is outside SDRAM")
                symbols[parts[2]] = address
        if set(symbols) != watched:
            raise ValueError("ELF is missing the expected probe symbols")
        env = {key: value for key, value in os.environ.items()
               if not key.startswith("VFLASH_") and key != "FLASHEM_BIOS"}
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   VFLASH_EXIT=str(frames), VFLASH_RAMDUMP=str(output / "ram.bin"),
                   VFLASH_RAMDUMP_FRAME=str(frames - 1), VFLASH_SHOT_FRAME=str(frames - 1),
                   VFLASH_SHOT=str(output / "frame.ppm"), VFLASH_RTC="0")
        if args.verify_animation:
            for shot in output.glob("frame-*.ppm"):
                shot.unlink()
            preview_frames = list(range(frames - 121, frames - 60, 6)) + [frames - 1]
            env.update(VFLASH_VELOG="1", VFLASH_SHOT_FRAME=",".join(map(str, preview_frames)),
                       VFLASH_SHOT=str(output / "frame-%d.ppm"))
        # Isolate ROM lookup and FlashEm's automatically written config.
        with tempfile.TemporaryDirectory(prefix="runtime-", dir=output) as runtime:
            runtime = Path(runtime)
            local_emulator = runtime / "flashem"
            shutil.copy2(emulator, local_emulator)
            rom = bios or runtime / "synthetic-rom.bin"
            if not bios:
                rom.write_bytes(struct.pack("<I", 0xEAFFFFFE))  # b .
            env["FLASHEM_BIOS"] = str(rom)
            command = [str(local_emulator), "--headless", "--language", "bios"]
            if disc:
                # ELF is used for symbols only, never injected into guest RAM.
                command.append(str(disc))
                env["VFLASH_TRACEPC"] = f'10c00010:{image["entry"]:08x},4'
            else:
                command += ["--load", str(elf)]
            with (output / "run.log").open("wb") as log:
                subprocess.run(command, env=env, cwd=runtime, stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=timeout)
            log_text = (output / "run.log").read_text(errors="replace")
            if str(rom) not in log_text:
                raise ValueError("emulator did not report loading the intended ROM")
            if disc and not all(f"[TRACE] {address:08X}" in log_text for address in (0x10C00010, image["entry"])):
                raise ValueError("firmware did not reach both the BOOT stub and ELF entry")
            # The baseline ROM has a few known unmodeled accesses. A new
            # unmodeled MMIO access from our own executable is a test failure.
            for line in log_text.splitlines():
                if line.startswith("[HW?]") and "PC=" in line:
                    pc = int(line.split("PC=", 1)[1].split()[0], 16)
                    if any(s["flags"] & 1 and s["address"] <= pc < s["address"] + s["file_size"]
                           for s in image["segments"]):
                        raise ValueError(f"unmodeled MMIO from application: {line}")
        ram = read_bounded(output / "ram.bin")
        if len(ram) != RAM_END - RAM_START:
            raise ValueError("expected a complete 16 MiB SDRAM dump")
        observed = {name: struct.unpack_from("<I", ram, symbols[name] - RAM_START)[0]
                    for name in expected}
        if observed != expected:
            raise ValueError(f"probe execution not established: observed {observed}")
        description = "original firmware disc boot; no --load" if disc else "direct ELF load only; synthetic ROM; no firmware/disc boot"
        result = dict(test=description,
                      elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                      emulator_sha256=hashlib.sha256(emulator.read_bytes()).hexdigest(),
                      symbols=symbols, observed=observed, passed=True)
        if disc:
            result.update(bios_sha256=hashlib.sha256(bios.read_bytes()).hexdigest(),
                          disc=str(disc), frames=frames)
        if args.verify_animation:
            from host_renderer import read_ppm, render_reference
            values = struct.unpack_from("<Iii5I", ram, symbols["animation_snapshot"] - RAM_START)
            snapshot = dict(zip(("frame", "x", "y", "fps", "shown_us", "bank", "draw_ticks", "frame_ticks"), values))
            if not (snapshot["frame"] >= 30 and 16 <= snapshot["x"] <= 280 and 84 <= snapshot["y"] <= 180
                    and 0 < snapshot["fps"] <= 120 and snapshot["bank"] == snapshot["frame"] % 2
                    and 0 < snapshot["draw_ticks"] <= snapshot["frame_ticks"]):
                raise ValueError(f"invalid animation/timing state: {snapshot}")
            banks = (symbols["video_pixels"], symbols["video_back_pixels"])
            flips = [int(value, 16) for value in re.findall(r"\[VE\] f\d+ layer 0 address ([0-9A-Fa-f]{8})", log_text)]
            flips = [value for value in flips if value in banks]
            if len(flips) < 30 or set(flips) != set(banks) or any(a == b for a, b in zip(flips, flips[1:])):
                raise ValueError("scanout did not alternate between both owned framebuffer banks")
            native, rgb, reference = render_reference(output / "reference", animation=snapshot)
            address = banks[snapshot["bank"]]
            actual = ram[address - RAM_START:address - RAM_START + len(native)]
            if actual != native:
                raise ValueError("animation front buffer differs from host rendering of the recorded state")
            final = output / f"frame-{frames - 1}.ppm"
            width, height, scanout = read_ppm(final)
            if (width, height) != (reference["width"], reference["height"]) or scanout != rgb:
                raise ValueError("actual animation scanout differs from the recorded CPU frame")
            old_w, old_h, previous = read_ppm(output / f"frame-{frames - 61}.ppm")
            if (old_w, old_h) != (width, height):
                raise ValueError("animation output dimensions changed")
            area = lambda pixels: b"".join(pixels[(y * width + 14) * 3:(y * width + 306) * 3] for y in range(82, 206))
            if area(previous) == area(scanout):
                raise ValueError("no visible square movement between captured frames")
            shutil.copy2(final, output / "frame.ppm")
            (output / "frame.bgr555").write_bytes(actual)
            result["animation"] = dict(snapshot, **reference, buffer_addresses=banks,
                                       observed_flips=len(flips), measured_draw_us=snapshot["draw_ticks"] * 100,
                                       capture_frames=preview_frames,
                                       verification="buffer alternation, motion, native RAM and actual RGB scanout match")
        elif args.verify_video:
            # This compiles draw/font/demo for the HOST and reads guest RAM;
            # it never supplies a framebuffer or expected pixels to FlashEm.
            from host_renderer import read_ppm, render_reference
            native, rgb, reference = render_reference(output / "reference")
            offset = symbols["video_pixels"] - RAM_START
            actual = ram[offset:offset + len(native)]
            if actual != native:
                raise ValueError("guest framebuffer differs from the same C renderer on the host")
            width, height, scanout = read_ppm(output / "frame.ppm")
            if (width, height) != (reference["width"], reference["height"]) or scanout != rgb:
                raise ValueError("FlashEm's actual scanout differs from the expected CPU-rendered image")
            (output / "frame.bgr555").write_bytes(actual)
            result["video"] = dict(reference, framebuffer_address=symbols["video_pixels"],
                                   framebuffer_bytes=len(actual),
                                   verification="all native RAM pixels and all presented RGB pixels match")
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Emulator smoke test failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
