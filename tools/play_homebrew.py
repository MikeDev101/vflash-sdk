"""Open homebrew, a dumped game, or the empty-drive BIOS menu in FlashEm."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MEMCARD_SIZE = 8 * 1024 * 1024


def find_dumped_discs(folder):
    """Discover disc descriptors; never mistake firmware/BOOT/card BINs for games."""
    folder = Path(folder)
    if not folder.is_dir():
        return []
    files = [p.resolve() for p in folder.rglob("*") if p.is_file() and p.suffix.lower() in (".cue", ".iso")]
    cue_stems = {(p.parent, p.stem.casefold()) for p in files if p.suffix.lower() == ".cue"}
    return sorted({p for p in files if p.suffix.lower() == ".cue" or
                   (p.parent, p.stem.casefold()) not in cue_stems}, key=lambda p: str(p).casefold())


def disc_label(path, folder):
    try:
        return str(path.relative_to(Path(folder).resolve()))
    except ValueError:
        return str(path)  # A dump may be a symlink to another drive.


def select_dumped_disc(folder, explicit=None):
    if explicit is not None:
        path = Path(explicit).expanduser().resolve()
        if not path.is_file() or path.suffix.lower() not in (".cue", ".iso", ".bin"):
            raise ValueError("--disc must name an existing CUE, ISO, or raw disc BIN")
        if path.suffix.lower() == ".bin":
            with path.open("rb") as stream:
                sync = stream.read(12)
            if sync != b"\0" + b"\xFF" * 10 + b"\0" or path.stat().st_size % 2352:
                raise ValueError("BIN is not a raw 2352-byte-sector CD image; prefer its CUE sheet")
        return path
    discs = find_dumped_discs(folder)
    if not discs:
        raise ValueError(f"no dumped discs found in {folder}; place extracted CUE/BIN pairs there")
    if len(discs) == 1:
        return discs[0]
    for index, disc in enumerate(discs, 1):
        print(f"{index}. {disc_label(disc, folder)}")
    if not sys.stdin.isatty():
        raise ValueError("multiple dumped discs found; use --disc PATH to select one")
    try:
        selection = int(input(f"Select disc [1-{len(discs)}]: "))
    except (ValueError, EOFError):
        raise ValueError("select a disc by number or use --disc PATH") from None
    if not 1 <= selection <= len(discs):
        raise ValueError("disc selection is outside the displayed range")
    return discs[selection - 1]


def prepare_memcard(path):
    """Preserve an existing raw card, or exclusively publish erased flash."""
    path = Path(path).expanduser().resolve()
    if path.exists():
        if not path.is_file() or path.stat().st_size != MEMCARD_SIZE:
            raise ValueError(f"memory card must be an 8 MiB raw image: {path}")
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=".new-card-", dir=path.parent, delete=False) as stream:
        temp = Path(stream.name)
        try:
            for _ in range(8):
                stream.write(b"\xFF" * (1024 * 1024))
            stream.close()
            os.link(temp, path)  # Exclusive: never replace an existing save.
        finally:
            temp.unlink(missing_ok=True)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, help="FlashEm executable (or set FLASHEM)")
    parser.add_argument("--bios", type=Path, help="local 2 MiB boot ROM")
    parser.add_argument("--scale", type=int, choices=(1, 2, 3, 4), default=3)
    parser.add_argument("--demo", choices=("hello", "animation"), help="homebrew demo (default: hello)")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--bios-menu", action="store_true", help="boot firmware with no disc; skip the homebrew build")
    modes.add_argument("--game", action="store_true", help="load a dumped game from dumps/; skip the homebrew build")
    parser.add_argument("--disc", type=Path, help="choose a specific dumped game image (requires --game)")
    parser.add_argument("--list", action="store_true", help="list dumped game images without launching (requires --game)")
    parser.add_argument("--memcard", nargs="?", type=Path, const=ROOT / "cards/card1.bin",
                        metavar="IMAGE", help="attach/create an 8 MiB card (default: cards/card1.bin)")
    args = parser.parse_args()
    if (args.disc is not None or args.list) and not args.game:
        parser.error("--disc and --list require game mode (use test-game.sh)")
    if args.demo is not None and (args.game or args.bios_menu):
        parser.error("--demo is only available in play-homebrew.sh")
    try:
        if args.list:
            for disc in find_dumped_discs(ROOT / "dumps"):
                print(disc_label(disc, ROOT / "dumps"))
            return 0
        animated = args.demo == "animation"
        disc = select_dumped_disc(ROOT / "dumps", args.disc) if args.game else ROOT / (
            "build/animation/homebrew.cue" if animated else "build/homebrew.cue")
        if args.emulator and not args.emulator.is_file():
            raise ValueError(f"emulator executable not found: {args.emulator}")
        if args.bios and not args.bios.is_file():
            raise ValueError(f"boot ROM not found: {args.bios}")
        candidates = [args.emulator, os.environ.get("FLASHEM"), ROOT / "build/emulator/flashem",
                      shutil.which("flashem")]
        emulator = next((Path(p).resolve() for p in candidates if p and Path(p).is_file()), None)
        if not emulator:
            raise ValueError("FlashEm not found: use --emulator /path/to/flashem or set FLASHEM")
        candidates = [args.bios, os.environ.get("FLASHEM_BIOS"), ROOT / "dumps/A29L160ATV@TSOP48.BIN",
                      ROOT / "A29L160ATV@TSOP48.BIN", ROOT / "dumps/70004.bin", ROOT / "70004.bin"]
        bios = next((Path(p).resolve() for p in candidates if p and Path(p).is_file()), None)
        if not bios or bios.stat().st_size != 2097152:
            raise ValueError("a local 2 MiB boot ROM is required; use --bios /path/to/ROM")
        card = prepare_memcard(args.memcard) if args.memcard is not None else None
        if not args.bios_menu and not args.game:
            if not (ROOT / "build/CMakeCache.txt").exists():
                subprocess.run(["cmake", "-S", str(ROOT), "-B", str(ROOT / "build"),
                                "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"], check=True)
            subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target",
                            "animation_disc_image" if animated else "disc_image"], check=True)
        runtime = ROOT / ("build/bios" if args.bios_menu else ("build/game" if args.game else (
            "build/play-animation" if animated else "build/play")))
        runtime.mkdir(parents=True, exist_ok=True)
        # Isolate ROM lookup and config from the source/build emulator directory.
        local_emulator = runtime / "flashem"
        if emulator != local_emulator:
            shutil.copy2(emulator, local_emulator)
        env = {k: v for k, v in os.environ.items()
               if not k.startswith("VFLASH_") and k not in ("FLASHEM_BIOS", "SDL_VIDEODRIVER", "SDL_AUDIODRIVER")}
        env["FLASHEM_BIOS"] = str(bios)
        # Static CPU rendering never submits GE lists. Disable the emulator's
        # GE-count-based fast-boot heuristic to keep normal interactive pacing.
        env["VFLASH_FASTBOOT"] = "0"
        command = [str(local_emulator), "--language", "bios", "--scale", str(args.scale)]
        if card is not None:
            command += ["--memcard", str(card)]
            print(f"Memory card: {card}", flush=True)
        if args.bios_menu:
            # main.c skips its automatic disc picker in running-debugger mode.
            # Execution starts unpaused at the ROM reset vector with no image.
            command.append("--dbg-run")
        else:
            command.append(str(disc))
        mode = "BIOS menu (empty drive)" if args.bios_menu else (f"dumped game: {disc.name}" if args.game else "firmware-booted homebrew")
        print(f"Opening {mode}. F11: fullscreen; F5: screenshot; Esc: quit.", flush=True)
        os.chdir(runtime)
        os.execve(str(local_emulator), command, env)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Cannot open emulator: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
