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
            self.assertFalse(any(addr in (0xa001, 0x6000)
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

    def test_mmc1_pinned_db_probe_and_selected_dr_mario_layout(self):
        # The mock's 512-byte PRG prefix has CRC32 6607DBDA, matching
        # the pinned Sanni metadata for Dr. Mario (Europe) MMC1 submapper 5.
        # No copyrighted PRG/CHR bytes are shipped in the fixture.
        base = {"RETRODE_MOCK_NES_PRG_KIB": "32",
                "RETRODE_MOCK_NES_CHR_KIB": "32",
                "RETRODE_MOCK_NES_DRMARIO_PROBE": "1"}
        log = os.path.join(self.temp.name, "drmario-probe-writes")
        probe = {"command": "probe", "core": "nes", "request_id": "probe"}
        events = self.run_cli(1, [probe],
                              dict(base, RETRODE_MOCK_NES_WRITELOG=log))
        self.assertEqual(events[-1]["event"], "probe_result", events)
        candidates = events[-1]["candidates"]
        drmario = [c for c in candidates if c["title"] == "Dr. Mario (Europe)"]
        self.assertEqual(len(drmario), 1, events)
        self.assertEqual(drmario[0]["mapper"], 1)
        self.assertEqual(drmario[0]["submapper"], 5)
        self.assertEqual(drmario[0]["prg_kib"], 32)
        self.assertEqual(drmario[0]["chr_kib"], 32)
        self.assertTrue(drmario[0]["read_supported"])
        self.assertEqual(events[-1]["mapping"], "mmc1")
        self.assertFalse(os.path.exists(log))

        events = self.run_cli(1, [probe], base)
        drmario = [c for c in events[-1]["candidates"]
                  if c["title"] == "Dr. Mario (Europe)"]
        self.assertEqual(len(drmario), 1)
        self.assertTrue(drmario[0]["read_supported"])
        job = {"command": "read_rom", "core": "nes",
               "request_id": "drmario-db", "layout_id": "m1s5-32-32",
               "output": os.path.join(self.temp.name, "drmario-db.nes")}
        events = self.run_cli(1, [job], base)
        self.assertEqual(events[-1]["event"], "result", events)
        self.assertFalse(events[-1]["verified"])
        self.assertEqual(events[-1]["layout_id"], "m1s5-32-32")
        with io.open(job["output"], "rb") as handle:
            data = handle.read()
        self.assertEqual(len(data), 16 + 64 * 1024)
        self.assertEqual(data[:3], b"NES")
        self.assertEqual(data[4:7], bytes(bytearray([2, 4, 0x10])))
        self.assertEqual(data[3], 0x1a)
        self.assertEqual(data[8], 0x50)  # pinned NES2 submapper 5 header

    def test_mmc1_manual_submapper5_unknown_uses_nes2_header(self):
        job = self.cmd(1, 32, 32, "mmc1-s5-unknown")
        job["submapper"] = 5
        events = self.run_cli(1, [job], {
            "RETRODE3_MMC1_TEST_ACK": "NO_BATTERY_TEST_ONLY",
            "RETRODE_MOCK_NES_PRG_KIB": "32",
            "RETRODE_MOCK_NES_CHR_KIB": "32"})
        self.assertEqual(events[-1]["event"], "result", events)
        self.assertFalse(events[-1]["verified"])
        self.assertEqual(events[-1]["layout_id"], "m1s5-32-32")
        with io.open(job["output"], "rb") as handle:
            data = handle.read()
        self.assertEqual(data[7] & 0x0c, 0x08)
        self.assertEqual(data[8] >> 4, 5)
        self.assertEqual(data[4], 2)
        self.assertEqual(data[5], 4)

    def test_mmc1_pinned_full_payload_crc_verifies_unique_title(self):
        # Synthetic 32/32 ROM: only 4 CHR bytes patched to the real
        # reference CRC. No original Nintendo ROM bytes are embedded.
        job = {"command": "read_rom", "core": "nes",
               "request_id": "drmario-verified", "layout_id": "m1s5-32-32",
               "output": os.path.join(self.temp.name, "drmario-verified.nes")}
        events = self.run_cli(1, [job], {
            "RETRODE3_MMC1_TEST_ACK": "NO_BATTERY_TEST_ONLY",
            "RETRODE_MOCK_NES_PRG_KIB": "32",
            "RETRODE_MOCK_NES_CHR_KIB": "32",
            "RETRODE_MOCK_NES_DRMARIO_PROBE": "1",
            "RETRODE_MOCK_NES_DRMARIO_CRC": "1"})
        result = events[-1]
        self.assertEqual(result["event"], "result", events)
        self.assertTrue(result["verified"], events)
        self.assertEqual(result["title"], "Dr. Mario (Europe)")
        self.assertEqual(result["payload_crc32"], "9735D267")
        self.assertTrue(result["matched_candidate_id"].startswith("mmc1-"))
        with io.open(job["output"], "rb") as handle:
            data = handle.read()
        self.assertEqual(data[8], 0x50)  # database submapper 5
        self.assertEqual(zlib.crc32(data[16:]) & 0xffffffff, 0x9735D267)

    def test_mmc1_chr_rom_and_prg_size_matrix(self):
        # Physical regression cases: 32K/32K and 128K/32K cartridges.
        # No separate laboratory build or environment acknowledgement.
        for p, c in ((32, 32), (128, 32), (128, 0),
                     (256, 64), (512, 0)):
            name = "mmc1-%d-%d" % (p, c)
            job = self.cmd(1, p, c, name)
            log = os.path.join(self.temp.name, name + "-writes")
            extra = {"RETRODE_MOCK_NES_PRG_KIB": str(p),
                     "RETRODE_MOCK_NES_CHR_KIB": str(c),
                     "RETRODE_MOCK_NES_WRITELOG": log}
            events = self.run_cli(1, [job], extra)
            result = events[-1]
            self.assertEqual(result["event"], "result", (name, events))
            self.assertEqual(result["layout_id"], "m1-%d-%d" % (p, c))
            self.assertFalse(result["verified"])
            with io.open(job["output"], "rb") as handle:
                data = handle.read()
            self.assertEqual(data, fixture_rom(1, p, c))
            self.assertEqual(result["bytes_written"], len(data))
            self.assertEqual(result["payload_crc32"],
                             "%08X" % (zlib.crc32(data[16:]) & 0xffffffff))
            with io.open(log, "r", encoding="utf-8") as handle:
                writes = [tuple(int(x) for x in line.split())
                          for line in handle if line.strip()]
            expected = (1 if p == 32 else (p // 16) * (
                16 if p == 512 else 11)) + (c // 8) * 11
            self.assertEqual(len(writes), expected)
            self.assertFalse(any(addr < 0x8000 for addr, _, _ in writes))

    def test_mmc1_chr_rom_is_available_without_lab_ack(self):
        job = self.cmd(1, 32, 32, "mmc1-normal")
        writes = os.path.join(self.temp.name, "mmc1-normal-writes")
        events = self.run_cli(1, [job], {
            "RETRODE_MOCK_NES_PRG_KIB": "32",
            "RETRODE_MOCK_NES_CHR_KIB": "32",
            "RETRODE_MOCK_NES_WRITELOG": writes})
        self.assertEqual(events[-1]["event"], "result", events)
        self.assertEqual(events[-1]["layout_id"], "m1-32-32")
        self.assertTrue(os.path.isfile(job["output"]))
        self.assertTrue(os.path.isfile(writes))

    def test_mmc1_battery_layout_remains_rejected(self):
        job = self.cmd(1, 128, 0, "mmc1-battery")
        job["battery"] = 1
        log = os.path.join(self.temp.name, "mmc1-battery-writes")
        events = self.run_cli(1, [job], {"RETRODE_MOCK_NES_WRITELOG": log})
        self.assertEqual(events[-1]["event"], "error", events)
        self.assertFalse(os.path.exists(job["output"]))
        self.assertFalse(os.path.exists(log))

    def test_mmc1_sgrom_bank_dump_crc_and_serial_order(self):
        job = self.cmd(1, 128, 0, "mmc1-sgrom")
        log = os.path.join(self.temp.name, "mmc1-sgrom-writes")
        events = self.run_cli(1, [job], {
            "RETRODE3_MMC1_NO_PRG_RAM_ACK":
                "RAD_RACER_SGROM_128K_CHRRAM_NO_WRAM",
            "RETRODE_MOCK_NES_WRITELOG": log})
        result = events[-1]
        self.assertEqual(result["event"], "result", events)
        self.assertFalse(result["verified"])
        self.assertEqual(result["layout_id"], "m1-128-0")
        with io.open(job["output"], "rb") as handle:
            data = handle.read()
        self.assertEqual(data, fixture_rom(1, 128, 0))
        self.assertEqual(result["crc32"],
                         "%08X" % (zlib.crc32(data) & 0xffffffff))
        self.assertEqual(result["payload_crc32"],
                         "%08X" % (zlib.crc32(data[16:]) & 0xffffffff))
        with io.open(log, "r", encoding="utf-8") as handle:
            writes = [tuple(int(v) for v in row.split())
                      for row in handle if row.strip()]
        self.assertEqual(len(writes), 8 * 11)
        for bank in range(8):
            seq = writes[bank * 11:(bank + 1) * 11]
            self.assertEqual(seq[0], (0x8000, 0x80, 10))
            self.assertEqual(seq[1:6],
                [(0x8000, (0x0c >> i) & 1, 10) for i in range(5)])
            self.assertEqual(seq[6:],
                [(0xe000, (bank >> i) & 1, 10) for i in range(5)])
        self.assertFalse(any(addr < 0x8000 for addr, value, mode in writes))

    def test_mmc1_sgrom_failed_write_does_not_publish(self):
        job = self.cmd(1, 128, 0, "mmc1-fault")
        events = self.run_cli(1, [job], {
            "RETRODE3_MMC1_NO_PRG_RAM_ACK":
                "RAD_RACER_SGROM_128K_CHRRAM_NO_WRAM",
            "RETRODE_MOCK_NES_WRITE_FAIL": "1"})
        self.assertEqual(events[-1]["event"], "error", events)
        self.assertFalse(os.path.exists(job["output"]))

    def test_mmc1_unsupported_submapper_remains_closed(self):
        job = self.cmd(1, 128, 64, "mmc1-invalid-submapper")
        job["submapper"] = 7
        log = os.path.join(self.temp.name, "mmc1-invalid-writes")
        events = self.run_cli(1, [job],
                              {"RETRODE_MOCK_NES_WRITELOG": log})
        self.assertEqual(events[-1]["event"], "error", events)
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
