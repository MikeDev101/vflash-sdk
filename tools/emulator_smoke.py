"""Prove probe execution by checking its RAM symbols.

The default ROM is our own four-byte ARM loop, only to satisfy FlashEm's
creation API. --load bypasses it. Supply both --bios and --disc instead to
test a real firmware-driven disc boot, with no direct program loading.
"""
import argparse
import concurrent.futures
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

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {"boot_marker": 0x56464C53, "startup_status": 0x600D600D, "data_cookie": 0x13579BDF}
SUITES = ("mmu-identity", "mmu-undefined", "input", "audio", "fs", "suite", "cpp", "fmv", "stress", "bad-apple")
INPUT_SEQUENCE = (
    "0x1@450-500;0x2@520-570;0x4@590-640;0x8@660-710;"
    "0x10@730-780;0x20@800-850;0x40@870-920;0x80@940-990;0x100@1010-1060"
)


def load_symbols(elf):
    out = subprocess.check_output(["arm-none-eabi-nm", "--defined-only", str(elf)], text=True)
    return {p[2]: int(p[0], 16) for line in out.splitlines() if len(p := line.split()) == 3}


def run_suite(suite, emulator, bios, out):
    out.mkdir(parents=True, exist_ok=True)
    if suite in ("mmu-identity", "mmu-undefined"):
        mode = suite.split("-", 1)[1]
        subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", f"mmu_{mode}_disc_image"],
                       check=True, stdout=subprocess.DEVNULL)
        result = out / "mmu-result.json"
        result.unlink(missing_ok=True)
        elf = ROOT / f"build/vflash_mmu_{mode}.elf"
        cmd = [sys.executable, str(ROOT / "tools/emulator_smoke.py"),
               "--emulator", str(emulator), "--bios", str(bios),
               "--elf", str(elf), "--disc", str(ROOT / f"build/mmu/{mode}/homebrew.cue"),
               "--out", str(out)]
        if mode == "identity":
            cmd.append("--verify-video")
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        sy = load_symbols(elf)
        ram = (out / "ram.bin").read_bytes()
        word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
        observed = {k: word(k) for k in ("mmu_status", "mmu_enabled_control", "mmu_disabled_control",
                                         "mmu_init_ticks", "mmu_tables_bytes", "mmu_probe")}
        if not (observed["mmu_status"] == 0x4D4D5531 and observed["mmu_probe"] == 0x1926ABCD
                and (observed["mmu_enabled_control"] & 1) and not (observed["mmu_disabled_control"] & 1)
                and not (observed["mmu_enabled_control"] & 0x1004)):
            raise ValueError(f"MMU bring-up failed: {observed}")
        if (sy["vflash_mmu_l1"] & 16383) or (sy["vflash_mmu_l2"] & 1023):
            raise ValueError("table alignment")
        if mode == "undefined":
            from host_renderer import read_ppm, to_rgb
            from panic_smoke import load_renderer, reference_pixels
            data = ram[sy["vflash_crash_record"] - 0x10000000:][:180]
            record = struct.unpack("<29I64s", data)
            if (record[0], record[2], record[3], record[4], record[7]) != (
                0x56435253, 3, 23, sy["mmu_fault_instruction"], sy["mmu_fault_instruction"] + 4
            ):
                raise ValueError("wrong CPU exception/PC")
            with tempfile.TemporaryDirectory() as tmp:
                native = reference_pixels(data, load_renderer(Path(tmp) / "panic.so"))
            if ram[sy["vflash_panic_pixels"] - 0x10000000:][:len(native)] != native:
                raise ValueError("panic RAM pixels")
            if read_ppm(out / "frame.ppm") != (320, 240, to_rgb(native)):
                raise ValueError("panic scanout")
        result.write_text(json.dumps(dict(
            passed=True, scope="translation only; no abort or permission validation",
            observed=observed, initialization_us=observed["mmu_init_ticks"] * 100
        ), indent=2) + "\n")
        print(result.read_text())
        return

    def run_variant(kind, mmu):
        v_out = out / f"{kind}-{mmu}"
        v_out.mkdir(parents=True, exist_ok=True)
        if suite == "input":
            elf = ROOT / (f"build/vflash_input_{mmu}.elf" if kind == "native" else f"build/vflash_sdl2_input_{mmu}.elf")
            disc = ROOT / (f"build/input/{mmu}/homebrew.cue" if kind == "native" else f"build/input/sdl2/{mmu}/homebrew.cue")
            extra = ["--input", INPUT_SEQUENCE, "--frames", "1100"]
        elif suite == "audio":
            elf = ROOT / (f"build/vflash_audio_{mmu}.elf" if kind == "native" else f"build/vflash_sdl2_audio_{mmu}.elf")
            disc = ROOT / (f"build/audio/{mmu}/homebrew.cue" if kind == "native" else f"build/audio/sdl2/{mmu}/homebrew.cue")
            extra = ["--frames", "600"]
        elif suite == "suite":
            elf = ROOT / f"build/vflash_suite_{mmu}.elf"
            disc = ROOT / f"build/suite/{mmu}/homebrew.cue"
            seq = (
                "0x100@440-450;"
                "0x100@1020-1035;"
                "0x1@1060-1075;0x100@1095-1105;0x80@1135-1175;"
                "0x1@1205-1220;0x100@1240-1250;0x80@1280-1320;"
                "0x1@1350-1365;0x100@1385-1395"
            )
            extra = ["--input", seq, "--frames", "1440"]
        elif suite == "cpp":
            elf = ROOT / f"build/vflash_cpp_{mmu}.elf"
            disc = ROOT / f"build/cpp/{mmu}/homebrew.cue"
            extra = ["--frames", "500"]
        elif suite == "fmv":
            elf = ROOT / f"build/vflash_fmv_{mmu}.elf"
            disc = ROOT / f"build/fmv/{mmu}/homebrew.cue"
            seq = "0x100@470-490;0x100@510-530;0x10@550-565;0x20@580-595"
            extra = ["--input", seq, "--frames", "650"]
        elif suite == "stress":
            elf = ROOT / f"build/vflash_stress_{mmu}.elf"
            disc = ROOT / f"build/stress/{mmu}/homebrew.cue"
            extra = ["--frames", "960"]
        elif suite == "bad-apple":
            elf = ROOT / f"build/vflash_bad_apple_{mmu}.elf"
            disc = ROOT / f"build/bad_apple/{mmu}/homebrew.cue"
            extra = ["--frames", "600"]
        else:
            elf = ROOT / (f"build/vflash_fs_{mmu}.elf" if kind == "native" else f"build/vflash_sdl2_asset_{mmu}.elf")
            disc = ROOT / (f"build/fs/native/{mmu}/homebrew.cue" if kind == "native" else f"build/fs/sdl2/{mmu}/homebrew.cue")
            extra = ["--frames", "600" if kind == "native" else "800"]

        subprocess.run([
            sys.executable, str(ROOT / "tools/emulator_smoke.py"),
            "--emulator", str(emulator), "--bios", str(bios),
            "--elf", str(elf), "--disc", str(disc),
            *extra, "--out", str(v_out),
        ], check=True, stdout=subprocess.DEVNULL, timeout=180)
        sy = load_symbols(elf)
        ram = (v_out / "ram.bin").read_bytes()
        word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
        sword = lambda name: struct.unpack_from("<i", ram, sy[name] - 0x10000000)[0]
        assert word("startup_status") == 0x600D600D
        if suite == "input":
            if kind == "native":
                assert word("input_probe_status") == 0x19001900 and word("input_connected") == 1
                assert word("input_buttons_ever_seen") == 0x1FF and word("input_buttons_ever_pressed") == 0x1FF and word("input_buttons_ever_released") == 0x1FF
                assert (sword("input_min_axis_x"), sword("input_max_axis_x"), sword("input_min_axis_y"), sword("input_max_axis_y")) == (-32768, 32767, -32768, 32767)
            else:
                assert word("sdl2_input_status") == 0x5D121900 and word("sdl2_joy_opened") == 1 and word("sdl2_controller_opened") == 1
                assert word("sdl2_buttons_seen") == 0x1F and word("sdl2_axis_motion_count") >= 50
        elif suite == "audio":
            if kind == "native":
                assert word("audio_demo_active") == 1 and word("audio_demo_samples_played") >= 1000 and word("audio_demo_underruns") == 0
            else:
                assert word("sdl2_audio_active") == 1 and word("sdl2_audio_callback_calls") >= 10 and word("sdl2_audio_samples_played") >= 1000
        elif suite == "suite":
            assert word("suite_status") == 0x50415353
            assert word("suite_tests_run") == 8 and word("suite_tests_passed") == 8 and word("suite_tests_failed") == 0
            assert word("suite_results_mask") == 0xFF and word("suite_done_mask") == 0xFF
            assert word("suite_diag_visited_mask") == 0x7 and word("suite_current_test") == 9
        elif suite == "cpp":
            assert word("cpp_global_ctor_runs") == 1
            assert word("cpp_virtual_calls") >= 10
            assert word("cpp_template_ok") == 1
            assert word("cpp_dynamic_alloc_ok") == 1
            assert word("cpp_sdl2_frames") >= 20
            assert word("cpp_all_passed") == 1
        elif suite == "fmv":
            assert word("fmv_status") == 0x464D5631
            assert word("fmv_video_opened") == 1
            assert word("fmv_frames_decoded") >= 15
            assert word("fmv_audio_samples") >= 1000
            assert word("fmv_pause_tested") == 1
            assert word("fmv_stop_tested") == 1
            assert word("fmv_restart_tested") == 1
            assert word("fmv_all_passed") == 1
        elif suite == "stress":
            assert word("stress_status") == 0x50415353
            assert word("stress_sweep_passed") == 0x7F
            assert word("stress_frames") >= 70
            assert word("stress_audio_effective_rate") > 19000
            assert word("stress_avg_frame_us") > 0
            assert word("stress_jpeg_avg_us") > 0
        elif suite == "bad-apple":
            assert word("bad_apple_status") == 0x42415031
            assert word("bad_apple_frames") >= 15
            assert word("bad_apple_audio_samples") >= 1000
            assert word("bad_apple_passed") == 1
        else:
            if kind == "native":
                assert word("fs_demo_err") == 0 and word("fs_demo_tests_passed") == 8 and word("fs_demo_bytes_read") == 6308
            else:
                assert word("asset_demo_err") == 0 and word("asset_demo_cfg_loaded") == 1 and word("asset_demo_bmp_loaded") == 1 and word("asset_demo_wav_loaded") == 1
        return f"PASS {suite} {kind} {mmu}"

    suite_targets = {
        "input": ("input_off_disc_image", "input_on_disc_image", "sdl2_input_off_disc_image", "sdl2_input_on_disc_image"),
        "audio": ("audio_off_disc_image", "audio_on_disc_image", "sdl2_audio_off_disc_image", "sdl2_audio_on_disc_image"),
        "fs": ("fs_off_disc_image", "fs_on_disc_image", "sdl2_asset_off_disc_image", "sdl2_asset_on_disc_image"),
        "suite": ("suite_off_disc_image", "suite_on_disc_image"),
        "cpp": ("cpp_off_disc_image", "cpp_on_disc_image"),
        "fmv": ("fmv_off_disc_image", "fmv_on_disc_image"),
        "stress": ("stress_off_disc_image", "stress_on_disc_image"),
        "bad-apple": ("bad_apple_off_disc_image", "bad_apple_on_disc_image"),
    }[suite]
    subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", *suite_targets],
                   check=True, stdout=subprocess.DEVNULL)
    kinds = ("unified",) if suite in ("suite", "cpp", "fmv", "stress", "bad-apple") else ("native", "sdl2")
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        futs = [pool.submit(run_variant, kind, mmu) for mmu in ("off", "on") for kind in kinds]
        for f in futs:
            print(f.result(), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", default=ROOT / "build/emulator/flashem", type=Path)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--suite", choices=SUITES, help="run an acceptance suite (mmu-identity, mmu-undefined, input, audio, fs)")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--bios", type=Path, help="local original ROM; requires --disc (or --suite)")
    parser.add_argument("--disc", type=Path, help="homebrew CUE image; requires --bios")
    parser.add_argument("--frames", type=int, help="exit frame (default: 3 direct, 60 direct video, 1200 disc)")
    parser.add_argument("--timeout", type=float, help="host timeout in seconds (default: 30 direct, 120 disc)")
    parser.add_argument("--trace-video", action="store_true", help="log scanout address changes")
    parser.add_argument("--input", help="input events in format '<hex_mask>@<start_frame>-<end_frame>;...' for FlashEm VFLASH_INPUT")
    parser.add_argument("--watch-vectors", action="store_true", help="log writes to the tested firmware UND/SWI RAM slots")
    parser.add_argument("--verify-video", action="store_true",
                        help="compare video status, RAM framebuffer and actual scanout against the host C renderer")
    parser.add_argument("--verify-animation", action="store_true",
                        help="verify animated firmware boot, buffer flips, movement, timing and exact pixels")
    args = parser.parse_args()
    if args.suite:
        bios = args.bios or (ROOT / "dumps/A29L160ATV@TSOP48.BIN")
        out = args.out or (ROOT / f"build/{args.suite}-acceptance")
        run_suite(args.suite, args.emulator, bios, out)
        return 0
    if not args.elf or not args.out:
        parser.error("--elf and --out are required unless --suite is specified")
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
        if args.trace_video:
            env["VFLASH_VELOG"] = "1"
        if args.watch_vectors:
            env["VFLASH_WATCH"] = "1000ff84,8"
        # Isolate ROM lookup and FlashEm's automatically written config.
        with tempfile.TemporaryDirectory(prefix="runtime-", dir=output) as runtime:
            runtime = Path(runtime)
            local_emulator = runtime / "flashem"
            shutil.copy2(emulator, local_emulator)
            rom = bios or runtime / "synthetic-rom.bin"
            if not bios:
                rom.write_bytes(struct.pack("<I", 0xEAFFFFFE))  # b .
            env["FLASHEM_BIOS"] = str(rom)
            if args.input:
                env["VFLASH_INPUT"] = args.input
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
