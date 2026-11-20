"""Sanni-derived NES mapper register sequencing against a strict mocked bus.
Python 3.4-compatible test logic. No physical writes are permitted by default.
"""
import io
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "build/migration/retrode3-cli-mock")
MODULE = os.path.join(ROOT, "build/migration/real-nes-fixture/nes.so")
MANIFEST = os.path.join(ROOT, "packaging/cores/nes.core")


def fixture_rom(mapper, prg_kib, chr_kib):
    header = bytearray(16)
    header[:4] = b"NES\x1a"
    header[4] = prg_kib // 16
    header[5] = chr_kib // 8
    header[6] = (mapper & 15) << 4
    header[7] = mapper & 0xf0
    prg = bytearray((i * 7 + (i >> 8) * 11 + (i >> 13) * 33 + 13) & 255
                    for i in range(prg_kib * 1024))
    chr_data = bytearray((i * 5 + (i >> 8) * 17 +
                          (i >> 10) * 41 + 9) & 255
                         for i in range(chr_kib * 1024))
    return bytes(header + prg + chr_data)


class TestNesMapperBanks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.temp.name, "available")
        self.enabled = os.path.join(self.temp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        shutil.copyfile(MODULE, os.path.join(self.available, "nes.so"))
        shutil.copyfile(MANIFEST, os.path.join(self.available, "nes.core"))
        os.symlink(os.path.join(self.available, "nes.so"),
                   os.path.join(self.enabled, "nes.so"))

    def tearDown(self):
        self.temp.cleanup()

    def run_cli(self, mapper, commands, extra=None):
        env = os.environ.copy()
        env["RETRODE_MOCK_NES_MAPPER"] = str(mapper)
        if extra:
            env.update(extra)
        args = [HOST, "--protocol", "ndjson-v1", "--interactive",
                "--available-dir", self.available,
                "--enabled-dir", self.enabled]
        process = subprocess.Popen(args, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, env=env)
        stdout, stderr = process.communicate(
            "".join(json.dumps(x) + "\n" for x in commands).encode("utf-8"))
        result = [json.loads(x) for x in stdout.decode("utf-8").splitlines()]
        self.assertEqual(process.returncode, 0,
                         stderr.decode("utf-8", "replace"))
        return result

    def cmd(self, mapper, prg, chr_size, name):
        return {"command": "read_rom", "core": "nes", "request_id": name,
                "output": os.path.join(self.temp.name, name + ".nes"),
                "mapper": mapper, "submapper": 0, "prg_kib": prg,
                "chr_kib": chr_size, "mirroring": "horizontal", "battery": 0}

    def test_bank_order_and_crc_for_mappers_2_3_4_7(self):
        for mapper, p, c in ((2, 64, 0), (3, 32, 32),
                             (4, 128, 64), (7, 128, 0)):
            job = self.cmd(mapper, p, c, "mapper-%s" % mapper)
            write_log = os.path.join(self.temp.name, "writes-%s" % mapper)
            events = self.run_cli(mapper, [job],
                                  {"RETRODE_MOCK_NES_WRITELOG": write_log})
            result = events[-1]
            self.assertEqual(result["event"], "result", (mapper, events))
            self.assertFalse(result["verified"])
            self.assertEqual(result["layout_id"], "m%s-%s-%s" % (mapper, p, c))
            with io.open(job["output"], "rb") as handle:
                actual = handle.read()
            self.assertEqual(actual, fixture_rom(mapper, p, c))
            self.assertEqual(result["crc32"],
                             "%08X" % (zlib.crc32(actual) & 0xffffffff))
            self.assertEqual(result["payload_crc32"],
                             "%08X" % (zlib.crc32(actual[16:]) & 0xffffffff))
            with io.open(write_log, "r", encoding="utf-8") as handle:
                writes = [tuple(int(v) for v in line.split())
                          for line in handle if line.strip()]
            self.assertTrue(writes)
            self.assertTrue(all(addr >= 0x8000 and mode == 10
                                for addr, value, mode in writes))
            self.assertFalse(any(addr in (0xa001, 0x6000, 0xe000)
                                 for addr, value, mode in writes))
            if mapper == 2:
                self.assertEqual(len(writes), 4)
                self.assertTrue(all(0xc000 <= addr <= 0xffff
                                    for addr, value, mode in writes))
            if mapper == 3:
                self.assertEqual(len(writes), 4)
            if mapper == 4:
                self.assertEqual(len(writes), 2 * (p // 8 + c))
            if mapper == 7:
                self.assertEqual(len(writes), p // 32)

    def test_probe_does_not_write_any_registers(self):
        for mapper in (2, 3, 4, 7):
            log = os.path.join(self.temp.name, "probe-%s.log" % mapper)
            events = self.run_cli(mapper, [
                {"command": "probe", "core": "nes", "request_id": "probe"}],
                {"RETRODE_MOCK_NES_WRITELOG": log})
            self.assertEqual(events[-1]["event"], "probe_result", events)
            self.assertFalse(os.path.exists(log))

    def test_mmc1_is_fail_closed_even_with_manual_override(self):
        job = self.cmd(1, 128, 64, "mmc1")
        log = os.path.join(self.temp.name, "mmc1-writes")
        events = self.run_cli(1, [job],
                              {"RETRODE_MOCK_NES_WRITELOG": log})
        self.assertEqual(events[-1]["event"], "error")
        self.assertFalse(os.path.exists(job["output"]))
        self.assertFalse(os.path.exists(log))

    def test_bus_conflicts_without_safe_address_fail_before_writes(self):
        for mapper, p, c in ((2, 64, 0), (3, 32, 32)):
            job = self.cmd(mapper, p, c, "no-safe-%s" % mapper)
            log = os.path.join(self.temp.name, "no-safe-writes-%s" % mapper)
            events = self.run_cli(mapper, [job],
                         {"RETRODE_MOCK_NES_NO_CONFLICT_SAFE": "1",
                          "RETRODE_MOCK_NES_WRITELOG": log})
            self.assertEqual(events[-1]["event"], "error", events)
            self.assertIn("bus-conflict-safe", events[-1]["message"])
            self.assertFalse(os.path.exists(job["output"]))
            self.assertFalse(os.path.exists(log))

    def test_failed_bank_write_does_not_publish_partial(self):
        job = self.cmd(4, 128, 64, "write-fail")
        events = self.run_cli(4, [job],
                              {"RETRODE_MOCK_NES_WRITE_FAIL": "1"})
        self.assertEqual(events[-1]["event"], "error", events)
        self.assertFalse(os.path.exists(job["output"]))
        self.assertFalse(any(name.startswith("write-fail.nes.partial-")
                             for name in os.listdir(self.temp.name)))

    def test_unsafe_mapper_sizes_and_submappers_rejected(self):
        for m, p, c, sub in ((2, 32, 0, 0), (3, 128, 32, 0),
                             (4, 64, 300, 0), (7, 128, 8, 0),
                             (2, 64, 0, 1)):
            job = self.cmd(m, p, c, "invalid-%s-%s" % (m, sub))
            job["submapper"] = sub
            events = self.run_cli(m, [job])
            self.assertEqual(events[-1]["event"], "error", events)
            self.assertFalse(os.path.exists(job["output"]))


if __name__ == "__main__":
    unittest.main()
