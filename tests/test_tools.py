import random
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from elf_image import read_elf
from inspect_boot import inspect
from make_iso import build_iso


def boot_fixture():
    # Synthetic observations only; NOT a valid commercial BOOT format fixture.
    return b"BOOT" + struct.pack("<5I", 36, 0x10C00000, 16, 0xE59FF000, 0) + struct.pack("<I", 0x10C00020) + b"REL\0DBG\0"


def elf_fixture():
    data = bytearray(88)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2, 40, 1, 0x10C00000, 52, 0, 0x05000200, 52, 32, 1, 40, 0, 0)
    struct.pack_into("<8I", data, 52, 1, 84, 0x10C00000, 0x10C00000, 4, 8, 5, 4)
    return data


class BootInspectorTests(unittest.TestCase):
    def test_observations(self):
        result = inspect(boot_fixture())
        self.assertEqual(result["load_address"], 0x10C00000)
        self.assertEqual(result["entry_literal_offset"], 24)
        self.assertEqual(result["entry_literal_value"], 0x10C00020)
        self.assertEqual(result["signature_hits"]["REL\0"]["offsets"], [28])

    def test_truncation_and_magic(self):
        for length in range(20):
            with self.assertRaises(ValueError):
                inspect(boot_fixture()[:length])
        with self.assertRaises(ValueError):
            inspect(b"NOPE" + boot_fixture()[4:])

    def test_outside_literal(self):
        for word in (0xE59FFFFF, 0xE51FF100):
            data = bytearray(boot_fixture())
            struct.pack_into("<I", data, 16, word)
            with self.assertRaises(ValueError):
                inspect(data)

    def test_unknown_entry_and_bad_load_are_reported(self):
        data = bytearray(boot_fixture())
        struct.pack_into("<I", data, 8, 0xFFFFFFFC)
        struct.pack_into("<I", data, 16, 0)
        result = inspect(data)
        self.assertNotIn("entry_literal_value", result)
        self.assertEqual(len(result["warnings"]), 3)

    def test_size_mismatch_is_reported(self):
        data = bytearray(boot_fixture())
        struct.pack_into("<I", data, 4, 32)
        self.assertIn("Image-size word differs", inspect(data)["warnings"][-1])

    def test_invalid_entry_offset(self):
        for offset in (0xFFFFFFFF, 0x1000, 17):
            data = bytearray(boot_fixture())
            struct.pack_into("<I", data, 12, offset)
            with self.assertRaises(ValueError):
                inspect(data)

    def test_signature_scan_bounded(self):
        result = inspect(boot_fixture() + b"DBG\0" * 1000)
        self.assertEqual(len(result["signature_hits"]["DBG\0"]["offsets"]), 32)
        self.assertTrue(result["signature_hits"]["DBG\0"]["capped"])

    def test_random_inputs_fail_cleanly(self):
        rng = random.Random(926)
        for length in range(512):
            data = rng.randbytes(length)
            try:
                inspect(data)
            except ValueError:
                pass
        # Exercise unknown header words without requiring a known chunk schema.
        for _ in range(1000):
            data = b"BOOT" + rng.randbytes(60)
            try:
                inspect(data)
            except ValueError:
                pass


class ElfTests(unittest.TestCase):
    def test_good_segment(self):
        self.assertEqual(read_elf(elf_fixture())["entry"], 0x10C00000)

    def test_bad_ranges_and_target(self):
        for offset, value, form in ((18, 3, "<H"), (28, 0xFFFFFFF0, "<I"),
                                   (56, 0xFFFFFFF0, "<I"), (72, 0xFFFFFFFF, "<I"),
                                   (68, 9, "<I"), (80, 3, "<I"), (76, 7, "<I"),
                                   (24, 0x10C00001, "<I"), (36, 0x05000400, "<I")):
            data = elf_fixture()
            struct.pack_into(form, data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                read_elf(data)

    def test_all_truncations(self):
        for length in range(88):
            with self.assertRaises(ValueError):
                read_elf(elf_fixture()[:length])


class IsoTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("isoinfo") and any(shutil.which(x) for x in ("xorriso", "genisoimage", "mkisofs")), "ISO tools unavailable")
    def test_payload_round_trip_and_no_overwrite(self):
        with tempfile.TemporaryDirectory(prefix="vflash test spaces ") as temp:
            boot = Path(temp) / "source boot.bin"
            iso = Path(temp) / "candidate image.iso"
            boot.write_bytes(boot_fixture())
            build_iso(boot, iso)
            result = subprocess.run(["isoinfo", "-i", str(iso), "-x", "/0SYSTEM/BOOT.BIN;1"],
                                    check=True, capture_output=True)
            self.assertEqual(result.stdout, boot.read_bytes())
            with self.assertRaises(ValueError):
                build_iso(boot, iso)


if __name__ == "__main__":
    unittest.main()
