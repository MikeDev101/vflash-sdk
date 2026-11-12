from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/external_game"


class ExternalConsumerTests(unittest.TestCase):
    def test_out_of_tree_cmake_consumer_builds_and_verifies(self):
        with tempfile.TemporaryDirectory() as temp:
            build_dir = Path(temp) / "build"
            toolchain = ROOT / "cmake/toolchain-vflash.cmake"
            cmake_prefix = ROOT / "cmake"

            # 1. Configure out-of-tree project via find_package(VFlash)
            subprocess.run([
                "cmake",
                "-S", str(FIXTURE),
                "-B", str(build_dir),
                f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                f"-DCMAKE_PREFIX_PATH={cmake_prefix}",
            ], check=True, stdout=subprocess.DEVNULL)

            # 2. Build target duck_test_disc
            subprocess.run([
                "cmake",
                "--build", str(build_dir),
                "--target", "duck_test_disc",
                "-j4",
            ], check=True, stdout=subprocess.DEVNULL)

            # 3. Verify outputs
            elf = build_dir / "duck_test.elf"
            boot_bin = build_dir / "disc/BOOT.BIN"
            iso = build_dir / "disc/homebrew.iso"
            cue = build_dir / "disc/homebrew.cue"
            bin_disc = build_dir / "disc/homebrew.bin"

            self.assertTrue(elf.is_file(), "ELF executable missing")
            self.assertTrue(boot_bin.is_file(), "BOOT.BIN missing")
            self.assertTrue(iso.is_file(), "ISO image missing")
            self.assertTrue(cue.is_file(), "CUE sheet missing")
            self.assertTrue(bin_disc.is_file(), "BIN disc image missing")

            # 4. Verify ELF passes strict memory contract
            verify_script = ROOT / "tools/verify_elf.py"
            subprocess.run([
                "python3", str(verify_script), str(elf),
                "--readelf", "arm-none-eabi-readelf",
            ], check=True, stdout=subprocess.DEVNULL)

            # 5. Verify BOOT.BIN header via inspect_boot
            import json
            inspect_script = ROOT / "tools/inspect_boot.py"
            res = subprocess.run([
                "python3", str(inspect_script), str(boot_bin),
            ], check=True, capture_output=True, text=True)
            data = json.loads(res.stdout)
            self.assertEqual(data["load_address"], 0x10C00000)
            self.assertEqual(data["entry_literal_value"], 0x10C00018)


if __name__ == "__main__":
    unittest.main()
