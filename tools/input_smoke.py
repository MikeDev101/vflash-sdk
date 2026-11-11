"""Verify native V.Flash controller input HAL and upstream SDL2 joystick integration.

Tests both native HAL (UART0 packet framing, 6-bit decoding, axis normalization,
edge detection, and cursor manipulation) and upstream SDL2 joystick/gamecontroller drivers
under firmware-booted FlashEm with deterministic input injection across MMU off and on.
"""
import argparse
import concurrent.futures
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

INPUT_SEQUENCE = (
    "0x1@450-500;0x2@520-570;0x4@590-640;0x8@660-710;"
    "0x10@730-780;0x20@800-850;0x40@870-920;0x80@940-990;0x100@1010-1060"
)

def run_native(mmu, emulator, bios, base):
    out = base / f"native-{mmu}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_input_{mmu}.elf"
    disc = ROOT / f"build/input/{mmu}/homebrew.cue"

    subprocess.run([
        sys.executable, str(ROOT / "tools/emulator_smoke.py"),
        "--emulator", str(emulator), "--bios", str(bios),
        "--elf", str(elf), "--disc", str(disc),
        "--input", INPUT_SEQUENCE,
        "--frames", "1100",
        "--out", str(out),
    ], check=True, stdout=subprocess.DEVNULL, timeout=180)

    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    sword = lambda name: struct.unpack_from("<i", ram, sy[name] - 0x10000000)[0]

    assert word("startup_status") == 0x600D600D, f"startup_status mismatch: {hex(word('startup_status'))}"
    assert word("input_probe_status") == 0x19001900, f"input_probe_status mismatch: {hex(word('input_probe_status'))}"
    assert word("input_connected") == 1, "controller not detected as connected"
    assert word("input_poll_count") >= 100, f"insufficient polls: {word('input_poll_count')}"
    assert word("input_buttons_ever_seen") == 0x1FF, f"buttons ever seen mismatch: {hex(word('input_buttons_ever_seen'))}"
    assert word("input_buttons_ever_pressed") == 0x1FF, f"buttons ever pressed mismatch: {hex(word('input_buttons_ever_pressed'))}"
    assert word("input_buttons_ever_released") == 0x1FF, f"buttons ever released mismatch: {hex(word('input_buttons_ever_released'))}"
    assert sword("input_min_axis_x") == -32768, f"min_axis_x mismatch: {sword('input_min_axis_x')}"
    assert sword("input_max_axis_x") == 32767, f"max_axis_x mismatch: {sword('input_max_axis_x')}"
    assert sword("input_min_axis_y") == -32768, f"min_axis_y mismatch: {sword('input_min_axis_y')}"
    assert sword("input_max_axis_y") == 32767, f"max_axis_y mismatch: {sword('input_max_axis_y')}"
    assert sword("input_cursor_x") != 160 or sword("input_cursor_y") != 180, "cursor did not move with analog axes"

    stats = {
        "poll_count": word("input_poll_count"),
        "buttons_ever_seen": hex(word("input_buttons_ever_seen")),
        "buttons_ever_pressed": hex(word("input_buttons_ever_pressed")),
        "buttons_ever_released": hex(word("input_buttons_ever_released")),
        "min_axis_x": sword("input_min_axis_x"),
        "max_axis_x": sword("input_max_axis_x"),
        "min_axis_y": sword("input_min_axis_y"),
        "max_axis_y": sword("input_max_axis_y"),
        "final_cursor_x": sword("input_cursor_x"),
        "final_cursor_y": sword("input_cursor_y"),
    }
    result.write_text(json.dumps(dict(
        passed=True, type="native", mmu=mmu, stats=stats
    ), indent=2) + "\n")
    print(f"PASS native_input_{mmu}: {stats['poll_count']} polls, buttons={stats['buttons_ever_seen']}, axes [{stats['min_axis_x']}..{stats['max_axis_x']}, {stats['min_axis_y']}..{stats['max_axis_y']}]", flush=True)


def run_sdl2(mmu, emulator, bios, base):
    out = base / f"sdl2-{mmu}"
    out.mkdir(parents=True, exist_ok=True)
    result = out / "result.json"
    result.unlink(missing_ok=True)
    elf = ROOT / f"build/vflash_sdl2_input_{mmu}.elf"
    disc = ROOT / f"build/input/sdl2/{mmu}/homebrew.cue"

    subprocess.run([
        sys.executable, str(ROOT / "tools/emulator_smoke.py"),
        "--emulator", str(emulator), "--bios", str(bios),
        "--elf", str(elf), "--disc", str(disc),
        "--input", INPUT_SEQUENCE,
        "--frames", "1100",
        "--out", str(out),
    ], check=True, stdout=subprocess.DEVNULL, timeout=180)

    sy = {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True).splitlines() if len(p := line.split()) == 3}
    ram = (out / "ram.bin").read_bytes()
    word = lambda name: struct.unpack_from("<I", ram, sy[name] - 0x10000000)[0]
    sword = lambda name: struct.unpack_from("<i", ram, sy[name] - 0x10000000)[0]

    assert word("startup_status") == 0x600D600D, f"startup_status mismatch: {hex(word('startup_status'))}"
    assert word("video_status") == 0x56494431, f"video_status mismatch: {hex(word('video_status'))}"
    assert word("render_status") == 0x44524157, f"render_status mismatch: {hex(word('render_status'))}"
    assert word("sdl2_input_status") == 0x5D121900, f"sdl2_input_status mismatch: {hex(word('sdl2_input_status'))}"
    assert word("sdl2_num_joysticks") >= 1, "no SDL joysticks reported"
    assert word("sdl2_joy_opened") == 1, "SDL_JoystickOpen failed"
    assert word("sdl2_controller_opened") == 1, "SDL_GameControllerOpen failed"
    assert word("sdl2_frames") >= 30, f"insufficient SDL2 frames rendered: {word('sdl2_frames')}"
    assert word("sdl2_axis_motion_count") >= 50, f"insufficient joy axis events: {word('sdl2_axis_motion_count')}"
    assert word("sdl2_button_down_count") >= 4, f"insufficient joy button down events: {word('sdl2_button_down_count')}"
    assert word("sdl2_button_up_count") >= 4, f"insufficient joy button up events: {word('sdl2_button_up_count')}"
    assert word("sdl2_controller_axis_count") >= 50, f"insufficient controller axis events: {word('sdl2_controller_axis_count')}"
    assert word("sdl2_controller_button_down_count") >= 4, f"insufficient controller button down events: {word('sdl2_controller_button_down_count')}"
    assert word("sdl2_controller_button_up_count") >= 4, f"insufficient controller button up events: {word('sdl2_controller_button_up_count')}"
    assert word("sdl2_buttons_seen") == 0x1F, f"SDL2 buttons seen mismatch: {hex(word('sdl2_buttons_seen'))}"

    stats = {
        "frames": word("sdl2_frames"),
        "joysticks": word("sdl2_num_joysticks"),
        "joy_axes": word("sdl2_axis_motion_count"),
        "joy_buttons_seen": hex(word("sdl2_buttons_seen")),
        "controller_axes": word("sdl2_controller_axis_count"),
        "controller_buttons_down": word("sdl2_controller_button_down_count"),
    }
    result.write_text(json.dumps(dict(
        passed=True, type="sdl2", mmu=mmu, stats=stats
    ), indent=2) + "\n")
    print(f"PASS sdl2_input_{mmu}: {stats['frames']} frames, joy_axes={stats['joy_axes']}, buttons={stats['joy_buttons_seen']}, ctrl_btn_down={stats['controller_buttons_down']}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, default=ROOT / "build/emulator/flashem")
    parser.add_argument("--bios", type=Path, default=ROOT / "dumps/A29L160ATV@TSOP48.BIN")
    parser.add_argument("--out", type=Path, default=ROOT / "build/input-acceptance")
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

    print("\nALL 4/4 INPUT ACCEPTANCE TESTS PASSED (Native HAL + SDL2 across MMU off and on)!")

if __name__ == "__main__":
    main()
