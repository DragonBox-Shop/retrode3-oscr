"""Read-only native OSCR MegaDrive stage-1 integration tests."""
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build/migration/oscr-md-native-mock"
LIB = ROOT / "build/migration/liboscr-md-core.a"
GENERATED = ROOT / "build/migration/oscr_md_core.cpp"


class TestNativeMd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for path in (EXE, LIB, GENERATED):
            if not path.is_file():
                raise RuntimeError("Run make -f Makefile.migration test-native-md")

    def call(self, *args, **kw):
        proc = subprocess.Popen([str(EXE)] + list(args), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=kw.get("env"))
        out, err = proc.communicate()
        return proc.returncode, json.loads((out if proc.returncode == 0 else err).decode("utf-8"))

    def test_original_cpp_header_probe(self):
        code, result = self.call("--action", "probe")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["engine"], "OSCR C++ MegaDrive readWord")
        self.assertEqual(result["header_size_bytes"], 524288)
        self.assertEqual(result["checksum"], "1234")
        self.assertEqual(result["overseas_title"], "MOCK MEGADRIVE CART")
        self.assertEqual(result["serial"], "GM MOCK0001-00")

    def test_native_archive_only_contains_core(self):
        members = subprocess.check_output(["ar", "t", str(LIB)]).decode("ascii")
        self.assertIn("oscr_md_core.o", members)
        self.assertNotIn("oscr_md_native.o", members)

    def test_read_rom_and_crc(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "cart.md"
            code, result = self.call("--action", "read-rom", "--output", str(out))
            self.assertEqual(code, 0, result)
            with io.open(str(out), "rb") as handle:
                rom = handle.read()
            self.assertEqual(len(rom), 524288)
            self.assertEqual(result["bytes_written"], len(rom))
            self.assertEqual(result["crc32"], "{:08X}".format(zlib.crc32(rom) & 0xffffffff))
            self.assertIsNone(result["verified"])
            self.assertEqual(rom[0x100:0x104], b"SEGA")
            self.assertEqual(rom[0x2000], 0x20)

    def test_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "keep.md"
            with io.open(str(out), "wb") as handle:
                handle.write(b"important")
            code, result = self.call("--action", "read-rom", "--output", str(out))
            self.assertEqual(code, 2)
            self.assertIn("exists", result["error"])
            with io.open(str(out), "rb") as handle:
                self.assertEqual(handle.read(), b"important")

    def test_reject_unknown_and_write_dependent_carts(self):
        for mode in ("header", "32x", "svp", "lockon", "size"):
            env = os.environ.copy()
            env["RETRODE_MOCK_MD_BAD"] = mode
            code, result = self.call("--action", "probe", env=env)
            self.assertEqual(code, 2, (mode, result))

    def test_reject_unsupported_actions_and_oversize(self):
        code, result = self.call("--action", "write-rom")
        self.assertEqual(code, 2, result)
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "bad.md"
            code, result = self.call("--action", "read-rom",
                                     "--size-bytes", "4194304", "--output", str(out))
            self.assertEqual(code, 2, result)
            self.assertFalse(out.exists())

    def test_upstream_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            original = ROOT / "third_party/oscr-firmware/src/cores/MegaDrive.cpp"
            with io.open(str(original), "r", encoding="utf-8") as handle:
                source = handle.read()
            # Modify only the original readWord() body, not earlier functions.
            modified = source
            begin = modified.index("  uint16_t readWord(uint32_t myAddress)")
            end = modified.index("  void writeFlash(", begin)
            region = modified[begin:end].replace("pulse_clock(10);", "pulse_clock(11);", 1)
            modified = modified[:begin] + region + modified[end:]
            upstream = Path(directory) / "MegaDrive.cpp"
            generated = Path(directory) / "core.cpp"
            with io.open(str(upstream), "w", encoding="utf-8") as handle:
                handle.write(modified)
            p = subprocess.Popen(
                ["python3", str(ROOT / "tools/build-linux-md-core.py"),
                 "--upstream", str(upstream), "--output", str(generated)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            _, err = p.communicate()
            self.assertEqual(p.returncode, 2)
            self.assertIn(b"review required", err)
            self.assertFalse(generated.exists())


if __name__ == "__main__":
    unittest.main()
