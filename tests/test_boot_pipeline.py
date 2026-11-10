import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pack_boot import pack_elf, PAYLOAD_BASE
from raw_cd import encode_sector, convert
from test_tools import elf_fixture


class BootPackerTests(unittest.TestCase):
    def test_header_and_determinism(self):
        data = elf_fixture()
        struct.pack_into("<I", data, 24, PAYLOAD_BASE)
        struct.pack_into("<II", data, 60, PAYLOAD_BASE, PAYLOAD_BASE)
        data[84:88] = b"test"
        boot = pack_elf(data)
        self.assertEqual(len(boot), 512)
        self.assertEqual(struct.unpack_from("<4s5I", boot),
                         (b"BOOT", 512, 0x10C00000, 16, 0xE51FF004, PAYLOAD_BASE))
        self.assertEqual(boot[24:28], b"test")
        self.assertEqual(boot[28:], bytes(484))
        self.assertEqual(boot, pack_elf(data))

    def test_reject_direct_load_layout(self):
        with self.assertRaises(ValueError):
            pack_elf(elf_fixture())


class RawSectorTests(unittest.TestCase):
    def test_independent_ecm_vector(self):
        # Generated independently with ECM at a8ec4e6, not our encoder.
        raw = encode_sector(bytes(range(256)) * 8, 1234)
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         "91505d232ba2afe6cd1658227e9730c344d0a1be24b9def31babf88faedf8c12")
        self.assertEqual(raw[12:16], bytes.fromhex("00 18 34 01"))

    def test_invalid_inputs(self):
        for data, lba in ((b"", 0), (bytes(2048), -1), (bytes(2048), 449850)):
            with self.assertRaises(ValueError):
                encode_sector(data, lba)

    def test_conversion_and_no_overwrite(self):
        with tempfile.TemporaryDirectory(prefix="raw cd spaces ") as temp:
            iso, cue = Path(temp) / "test.iso", Path(temp) / "test image.cue"
            iso.write_bytes(bytes(range(256)) * 16)
            convert(iso, cue)
            raw = cue.with_suffix(".bin").read_bytes()
            self.assertEqual(len(raw), 2 * 2352)
            self.assertEqual(raw[16:2064] + raw[2368:4416], iso.read_bytes())
            self.assertIn('TRACK 01 MODE1/2352', cue.read_text())
            with self.assertRaises(ValueError):
                convert(iso, cue)


if __name__ == "__main__":
    unittest.main()
