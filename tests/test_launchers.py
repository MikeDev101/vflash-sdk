from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from play_homebrew import MEMCARD_SIZE, prepare_memcard
import play_homebrew as launcher


class DumpedGameTests(unittest.TestCase):
    def test_panic_launcher_uses_a_separate_diagnostic_disc(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "build").mkdir()
            (root / "build/CMakeCache.txt").touch()
            bios, emulator = root / "bios.bin", root / "flashem"
            bios.write_bytes(bytes(2097152))
            emulator.touch()
            for mode in ("undefined", "manual", "thumb-swi"):
                arguments = ["play-homebrew.sh", "--demo", "panic", "--emulator", str(emulator), "--bios", str(bios)]
                if mode != "undefined":
                    arguments += ["--panic-test", mode]
                with self.subTest(mode=mode), patch.object(launcher, "ROOT", root), patch.object(sys, "argv", arguments), \
                     patch.object(launcher.subprocess, "run") as build, patch.object(launcher.shutil, "copy2"), \
                     patch.object(launcher.os, "chdir"), patch.object(launcher.os, "execve") as execute:
                    launcher.main()
                    self.assertEqual(build.call_args.args[0][-1], f"panic_{mode}_disc_image")
                    self.assertEqual(execute.call_args.args[1][-1], str(root / f"build/panic/{mode}/homebrew.cue"))
                    self.assertNotIn("--load", execute.call_args.args[1])

    def test_animation_has_its_own_build_target_and_disc(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "dumps").mkdir()
            (root / "build").mkdir()
            (root / "build/CMakeCache.txt").touch()
            bios = root / "dumps/70004.bin"
            bios.write_bytes(bytes(2097152))
            emulator = root / "flashem"
            emulator.touch()
            arguments = ["play-homebrew.sh", "--demo", "animation", "--emulator", str(emulator), "--bios", str(bios)]
            with patch.object(launcher, "ROOT", root), patch.object(sys, "argv", arguments), \
                 patch.object(launcher.subprocess, "run") as build, patch.object(launcher.shutil, "copy2"), \
                 patch.object(launcher.os, "chdir"), patch.object(launcher.os, "execve") as execute:
                launcher.main()
                self.assertEqual(build.call_args.args[0][-1], "animation_disc_image")
                self.assertEqual(execute.call_args.args[1][-1], str(root / "build/animation/homebrew.cue"))
                self.assertNotIn("--load", execute.call_args.args[1])

    def test_mmu_and_gpu_demos_resolve_their_build_targets_and_discs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "build").mkdir()
            (root / "build/CMakeCache.txt").touch()
            bios, emulator = root / "bios.bin", root / "flashem"
            bios.write_bytes(bytes(2097152))
            emulator.touch()
            cases = [
                (["--demo", "mmu-identity"], "mmu_identity_disc_image", "build/mmu/identity/homebrew.cue"),
                (["--demo", "mmu-undefined"], "mmu_undefined_disc_image", "build/mmu/undefined/homebrew.cue"),
                (["--demo", "mmu-animation"], "mmu_animation_disc_image", "build/mmu/animation/homebrew.cue"),
                (["--demo", "gpu-software"], "gpu_software_disc_image", "build/gpu/software/homebrew.cue"),
                (["--demo", "gpu-mmu"], "gpu_mmu_disc_image", "build/gpu/mmu/homebrew.cue"),
                (["--demo", "gpu-native"], "gpu_native_disc_image", "build/gpu/native/homebrew.cue"),
                (["--demo", "gpu-tiled"], "gpu_tiled_off_a_disc_image", "build/gpu/tiled/off/a/homebrew.cue"),
                (["--demo", "gpu-frame", "--gpu-mmu", "on", "--gpu-view", "b"],
                 "gpu_frame_on_b_disc_image", "build/gpu/frame/on/b/homebrew.cue"),
                (["--demo", "gpu-sprite", "--gpu-mmu", "on", "--gpu-view", "linear"],
                 "gpu_sprite_on_linear_disc_image", "build/gpu/sprite/on/linear/homebrew.cue"),
                (["--demo", "gpu-transparent", "--gpu-mmu", "on", "--gpu-view", "a"],
                 "gpu_transparent_on_a_disc_image", "build/gpu/transparent/on/a/homebrew.cue"),
                (["--demo", "gpu-alpha", "--gpu-mmu", "on", "--gpu-view", "b"],
                 "gpu_alpha_on_b_disc_image", "build/gpu/alpha/on/b/homebrew.cue"),
                (["--demo", "gpu-modes", "--gpu-mmu", "on", "--gpu-view", "linear"],
                 "gpu_modes_on_linear_disc_image", "build/gpu/modes/on/linear/homebrew.cue"),
            ]
            for extra, target, disc in cases:
                arguments = ["play-homebrew.sh", *extra, "--emulator", str(emulator), "--bios", str(bios)]
                with self.subTest(extra=extra), patch.object(launcher, "ROOT", root), patch.object(sys, "argv", arguments), \
                     patch.object(launcher.subprocess, "run") as build, patch.object(launcher.shutil, "copy2"), \
                     patch.object(launcher.os, "chdir"), patch.object(launcher.os, "execve") as execute:
                    launcher.main()
                    self.assertEqual(build.call_args.args[0][-1], target)
                    self.assertEqual(execute.call_args.args[1][-1], str(root / disc))
                    self.assertNotIn("--load", execute.call_args.args[1])

    def test_disc_discovery_prefers_cue_and_excludes_other_binaries(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ("A29L160ATV@TSOP48.BIN", "BOOT.BIN", "card.bin", "game.CUE", "game.iso", "other.ISO"):
                (root / name).touch()
            self.assertEqual([p.name for p in launcher.find_dumped_discs(root)], ["game.CUE", "other.ISO"])

    def test_single_game_auto_selection_and_explicit_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "BOOT.BIN").write_bytes(b"BOOT")
            with self.assertRaises(ValueError):
                launcher.select_dumped_disc(root)
            cue = root / "Game with spaces.cue"
            cue.touch()
            self.assertEqual(launcher.select_dumped_disc(root), cue.resolve())
            with self.assertRaises(ValueError):
                launcher.select_dumped_disc(root, root / "BOOT.BIN")

    def test_multiple_games_require_selection_without_a_terminal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "a.cue").touch()
            (root / "b.cue").touch()
            with patch.object(sys.stdin, "isatty", return_value=False), self.assertRaises(ValueError):
                launcher.select_dumped_disc(root)

    def test_game_mode_uses_firmware_and_skips_homebrew_build(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "dumps").mkdir()
            cue = root / "dumps" / "Game's disc.cue"
            cue.touch()
            bios = root / "dumps/70004.bin"
            bios.write_bytes(bytes(2097152))
            emulator = root / "flashem"
            emulator.touch()
            arguments = ["test-game.sh", "--game", "--emulator", str(emulator), "--bios", str(bios)]
            with patch.object(launcher, "ROOT", root), patch.object(sys, "argv", arguments), \
                 patch.object(launcher.subprocess, "run", side_effect=AssertionError("must not build homebrew")), \
                 patch.object(launcher.shutil, "copy2"), patch.object(launcher.os, "chdir"), \
                 patch.object(launcher.os, "execve") as execute:
                launcher.main()
                command = execute.call_args.args[1]
                self.assertEqual(command[-1], str(cue.resolve()))
                self.assertNotIn("--load", command)
                self.assertNotIn("--dbg-run", command)


class MemoryCardTests(unittest.TestCase):
    def test_new_card_is_erased_flash(self):
        with tempfile.TemporaryDirectory() as temp:
            card = Path(temp) / "cards with spaces" / "card.bin"
            self.assertEqual(prepare_memcard(card), card.resolve())
            self.assertEqual(card.read_bytes(), b"\xFF" * MEMCARD_SIZE)
            self.assertEqual(list(card.parent.iterdir()), [card])

    def test_existing_save_is_never_reset(self):
        with tempfile.TemporaryDirectory() as temp:
            card = Path(temp) / "saved.bin"
            data = b"existing save" + bytes(MEMCARD_SIZE - 13)
            card.write_bytes(data)
            prepare_memcard(card)
            self.assertEqual(card.read_bytes(), data)

    def test_wrong_size_is_rejected_without_modifying_input(self):
        with tempfile.TemporaryDirectory() as temp:
            card = Path(temp) / "wrong.bin"
            card.write_bytes(b"important data")
            with self.assertRaises(ValueError):
                prepare_memcard(card)
            self.assertEqual(card.read_bytes(), b"important data")


if __name__ == "__main__":
    unittest.main()
