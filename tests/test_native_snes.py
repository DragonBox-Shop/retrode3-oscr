"""Executable integration tests of original OSCR C++ LoROM/HiROM bank loops."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOCK = ROOT / "build/migration/oscr-snes-native-mock"
BANK_SOURCE = ROOT / "build/migration/oscr_snes_banks.cpp"


class TestNativeSnes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not MOCK.is_file() or not BANK_SOURCE.is_file():
            raise RuntimeError("Run make -f Makefile.migration test-native-snes")

    def call(self, *args):
        p = subprocess.Popen([str(MOCK)] + list(args),
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        stream = out if p.returncode == 0 else err
        return p.returncode, json.loads(stream.decode("utf-8"))

    def test_lorom_4_mib_uses_original_oscr_bank_order(self):
        self.test_image("lo", 4194304, 32768, 0x80)

    def test_hirom_4_mib_uses_original_oscr_bank_order(self):
        self.test_image("hi", 4194304, 65536, 0xc0)

    def test_lorom_small_uses_lower_banks(self):
        self.test_image("lo", 524288, 32768, 0)

    def test_hirom_small_uses_upper_banks(self):
        self.test_image("hi", 524288, 65536, 0xc0)

    def test_image(self, mode, size, bank_size, first_bank):
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / "rom.sfc"
            code, result = self.call("--mapping", mode, "--size-bytes",
                                     str(size), "--output", str(file))
            self.assertEqual(code, 0, result)
            self.assertEqual(result["engine"], "OSCR C++ bank readers")
            self.assertEqual(result["bytes_written"], size)
            payload = file.read_bytes()
            self.assertEqual(len(payload), size)
            for n in range(size // bank_size):
                bank = first_bank + n
                offset = 0x8000 if mode == "lo" else 0
                for pos in (0, 255, 256, 511, 512, bank_size - 1):
                    expected = (bank * 13 + ((offset + pos) >> 8)) & 255
                    self.assertEqual(payload[n * bank_size + pos], expected,
                                     (mode, bank, pos))

    def test_existing_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / "existing.sfc"
            file.write_bytes(b"keep me")
            code, result = self.call("--mapping", "lo", "--size-bytes",
                                     "524288", "--output", str(file))
            self.assertEqual(code, 2)
            self.assertFalse(result["ok"])
            self.assertEqual(file.read_bytes(), b"keep me")

    def test_invalid_layout_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = str(Path(temp) / "bad.sfc")
            for mode, size in (("lo", "0"), ("hi", "32768"),
                               ("lo", "4194305"), ("hi", "524289"),
                               ("special", "524288"), ("lo", "-524288")):
                code, result = self.call("--mapping", mode, "--size-bytes",
                                         size, "--output", path)
                self.assertEqual(code, 2)
                self.assertFalse(result["ok"])
                self.assertFalse(Path(path).exists())

    def test_generated_cpp_preserves_upstream_bank_loops(self):
        source = BANK_SOURCE.read_text(encoding="utf-8")
        self.assertIn("void readLoRomBanks(", source)
        self.assertIn("void readHiRomBanks(", source)
        self.assertEqual(source.count("while (currByte != 0);"), 2)
        self.assertEqual(source.count("OSCR::RetroDe3::readBlock("), 2)
        self.assertNotIn("buffer[c] = PINC;", source)


if __name__ == "__main__":
    unittest.main()
