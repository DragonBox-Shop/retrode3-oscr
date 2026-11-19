"""Executable integration tests of original OSCR C++ LoROM/HiROM bank loops."""
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOCK = ROOT / "build/migration/oscr-snes-native-mock"
BANK_SOURCE = ROOT / "build/migration/oscr_snes_banks.cpp"
BANK_LIBRARY = ROOT / "build/migration/liboscr-snes-core.a"

# Python 3.4 on the real Retrode3 has no pathlib Path.read_bytes,
# Path.write_bytes or Path.read_text (added in Python 3.5).
def read_bytes(path):
    with io.open(str(path), "rb") as handle:
        return handle.read()


def write_bytes(path, payload):
    with io.open(str(path), "wb") as handle:
        handle.write(payload)


def read_text(path):
    with io.open(str(path), "r", encoding="utf-8") as handle:
        return handle.read()


class TestNativeSnes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not MOCK.is_file() or not BANK_SOURCE.is_file() or not BANK_LIBRARY.is_file():
            raise RuntimeError("Run make -f Makefile.migration test-native-snes")

    def call(self, *args, **kwargs):
        p = subprocess.Popen([str(MOCK)] + list(args),
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             env=kwargs.get('env'))
        out, err = p.communicate()
        stream = out if p.returncode == 0 else err
        return p.returncode, json.loads(stream.decode("utf-8"))

    def ndjson(self, args, commands=None):
        process = subprocess.Popen(
            [str(MOCK)] + list(args),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = process.communicate(
            None if commands is None else commands.encode("utf-8"))
        self.assertFalse(err, err.decode("utf-8", "replace"))
        events = [json.loads(line) for line in out.decode("utf-8").splitlines()]
        return process.returncode, events

    def test_protocol_probe_preserves_one_shot_default(self):
        code, result = self.call("--action", "probe")
        self.assertEqual(code, 0)
        self.assertEqual(result["mapping"], "lo")
        self.assertNotIn("event", result)
        rc, events = self.ndjson(["--protocol", "ndjson-v1", "--action", "probe"])
        self.assertEqual(rc, 0)
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["version"], 1)
        self.assertEqual(events[0]["event"], "probe_result")
        self.assertEqual(events[0]["size_bytes"], 524288)
        self.assertEqual(events[0]["candidate_lookup"], "not_integrated")

    def test_protocol_streaming_real_byte_progress(self):
        with tempfile.TemporaryDirectory() as temp:
            output = str(Path(temp) / "progress.sfc")
            rc, events = self.ndjson(
                ["--protocol", "ndjson-v1", "--action", "read-rom",
                 "--output", output])
            self.assertEqual(rc, 0)
            progress = [x for x in events if x["event"] == "progress"]
            self.assertGreater(len(progress), 2)
            self.assertEqual(progress[0]["bytes_done"], 0)
            self.assertEqual(progress[-1]["bytes_done"], 524288)
            self.assertEqual(progress[-1]["percent"], 100)
            self.assertTrue(all(a["bytes_done"] <= b["bytes_done"]
                                for a, b in zip(progress, progress[1:])))
            self.assertEqual(events[-1]["event"], "result")
            self.assertIsNone(events[-1]["verified"])
            self.assertEqual(len(read_bytes(Path(output))), 524288)

    def test_interactive_probe_then_read(self):
        with tempfile.TemporaryDirectory() as temp:
            output = str(Path(temp) / "interactive.sfc")
            commands = (json.dumps({"command":"get_capabilities","request_id":"cap"}) + "\n"
                        + json.dumps({"command":"probe","request_id":"pr"}) + "\n"
                        + json.dumps({"command":"read_rom","request_id":"rd",
                                      "output":output}) + "\n")
            rc, events = self.ndjson(
                ["--protocol", "ndjson-v1", "--interactive"], commands)
            self.assertEqual(rc, 0)
            self.assertEqual(events[0]["event"], "ready")
            self.assertEqual(events[1]["event"], "capabilities")
            self.assertFalse(events[1]["candidate_selection"])
            self.assertEqual(events[2]["event"], "probe_result")
            self.assertEqual(events[-1]["event"], "result")
            self.assertEqual(events[-1]["request_id"], "rd")
            self.assertEqual(Path(output).stat().st_size, 524288)

    def test_interactive_cancel_removes_partial_file(self):
        with tempfile.TemporaryDirectory() as temp:
            output = str(Path(temp) / "cancelled.sfc")
            commands = (json.dumps({"command":"read_rom","request_id":"job-1",
                                     "output":output}) + "\n"
                        + json.dumps({"command":"cancel","request_id":"job-1"}) + "\n")
            rc, events = self.ndjson(
                ["--protocol", "ndjson-v1", "--interactive"], commands)
            self.assertEqual(rc, 0)
            self.assertEqual(events[-1]["event"], "error")
            self.assertEqual(events[-1]["code"], "cancelled")
            self.assertFalse(Path(output).exists())
            self.assertFalse(any(p.name.startswith("cancelled.sfc.partial")
                                 for p in Path(temp).iterdir()))

    def test_invalid_json_command_does_not_kill_session(self):
        commands = ('{"command":"probe","request_id":"bad","size_bytes":-1}\n'
                    '{"command":"probe","request_id":"good"}\n')
        rc, events = self.ndjson(
            ["--protocol", "ndjson-v1", "--interactive"], commands)
        self.assertEqual(rc, 0)
        self.assertEqual(events[1]["event"], "error")
        self.assertEqual(events[1]["code"], "invalid_command")
        self.assertEqual(events[2]["event"], "probe_result")
        self.assertEqual(events[2]["request_id"], "good")

    def test_oscr_core_is_linkable_archive_not_entire_firmware(self):
        archive_members = subprocess.check_output(
            ["ar", "t", str(BANK_LIBRARY)]).decode("ascii")
        self.assertIn("oscr_snes_banks.o", archive_members)
        self.assertNotIn("oscr_snes_native.o", archive_members)

    def test_original_oscr_checkcart_lorom_probe(self):
        code, result = self.call("--action", "probe")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["engine"], "OSCR C++ checkcart")
        self.assertEqual(result["mapping"], "lo")
        self.assertEqual(result["size_bytes"], 524288)
        self.assertEqual(result["chips"], 0)
        self.assertEqual(result["checksum"], "1234")
        self.assertEqual(result["title"], "MOCK SNES CART")

    def test_original_oscr_checkcart_hirom_probe(self):
        env = os.environ.copy()
        env["RETRODE_MOCK_MAP"] = "hi"
        code, result = self.call("--action", "probe", env=env)
        self.assertEqual(code, 0, result)
        self.assertEqual(result["mapping"], "hi")
        self.assertEqual(result["size_bytes"], 524288)

    def test_auto_mapping_and_size_from_original_checkcart(self):
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / "automatic.sfc"
            code, result = self.call("--action", "read-rom",
                                     "--output", str(file))
            self.assertEqual(code, 0, result)
            self.assertEqual(result["bytes_written"], 524288)
            self.assertEqual(result["mapping"], "lo")
            self.assertEqual(len(read_bytes(file)), 524288)

    def test_lorom_4_mib_uses_original_oscr_bank_order(self):
        self._check_image("lo", 4194304, 32768, 0x80)

    def test_hirom_4_mib_uses_original_oscr_bank_order(self):
        self._check_image("hi", 4194304, 65536, 0xc0)

    def test_lorom_small_uses_lower_banks(self):
        self._check_image("lo", 524288, 32768, 0)

    def test_hirom_small_uses_upper_banks(self):
        self._check_image("hi", 524288, 65536, 0xc0)

    def _check_image(self, mode, size, bank_size, first_bank):
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / "rom.sfc"
            code, result = self.call("--mapping", mode, "--size-bytes",
                                     str(size), "--output", str(file))
            self.assertEqual(code, 0, result)
            self.assertEqual(result["engine"], "OSCR C++ bank readers")
            self.assertEqual(result["bytes_written"], size)
            payload = read_bytes(file)
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
            write_bytes(file, b"keep me")
            code, result = self.call("--mapping", "lo", "--size-bytes",
                                     "524288", "--output", str(file))
            self.assertEqual(code, 2)
            self.assertFalse(result["ok"])
            self.assertEqual(read_bytes(file), b"keep me")

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
        source = read_text(BANK_SOURCE)
        self.assertIn("void readLoRomBanks(", source)
        self.assertIn("void readHiRomBanks(", source)
        self.assertEqual(source.count("while (currByte != 0);"), 2)
        self.assertEqual(source.count("OSCR::RetroDe3::readBlock("), 3)
        self.assertIn("bool checkcart()", source)
        self.assertNotIn("buffer[c] = PINC;", source)


if __name__ == "__main__":
    unittest.main()
