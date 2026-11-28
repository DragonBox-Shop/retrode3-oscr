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
PRODUCTION_MODULE = os.path.join(ROOT, "build/migration/real-nes-production-fixture/nes.so")


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

    def v3_override(self, prg, chr_size):
        return {"version": 1, "schema_id": "nes-layout-v1",
                "mode": "override", "parameters": {
                    "mapper": 1, "submapper": 0, "prg_kib": prg,
                    "chr_kib": chr_size, "mirroring": "horizontal",
                    "battery": False}}

    def test_v3_rad_racer_pal_is_not_highway_star_japan(self):
        # Actual PAL PRG CRC 654F4E90 from NES-SGROM and real hardware.
        # First 512 bytes intentionally have Sanni's Highway Star CRC.
        env = {"RETRODE_MOCK_NES_PRG_KIB": "128",
               "RETRODE_MOCK_NES_CHR_KIB": "0",
               "RETRODE_MOCK_NES_RAD_RACER_REF": "1"}
        log = os.path.join(self.temp.name, "rad-racer-probe-writes")
        probe = {"command": "probe", "core": "nes", "request_id": "rad-probe"}
        events = self.run_cli(1, [probe],
                              dict(env, RETRODE_MOCK_NES_WRITELOG=log))
        info = events[-1]
        self.assertEqual(info["event"], "probe_result", events)
        self.assertEqual(info["probe_fingerprints"]["cpu_8000_crc32"],
                         "402629F3")
        self.assertFalse(os.path.exists(log), "probe wrote mapper registers")
        candidates = info["candidates"]
        jp = [c for c in candidates if c["title"] ==
              "Highway Star (Japan) (Famicom 3D System)"]
        eu = [c for c in candidates if c["title"] == "Rad Racer (Europe)"]
        self.assertEqual(len(jp), 1)
        self.assertEqual(len(eu), 1)
        self.assertEqual(jp[0]["layout_id"], eu[0]["layout_id"])
        # Complete database reveals pirate boards sharing the same PRG
        # fingerprint. Do not silently auto-select MMC1 over other mappers.
        self.assertEqual(info["ambiguity"], "layouts")
        self.assertTrue(info["selection_required"])
        self.assertTrue({312, 418}.issubset({c["mapper"] for c in candidates}))
        self.assertTrue(all(c["read_supported"] for c in (jp[0], eu[0])))
        self.assertEqual(info["title"], "NES cartridge")

        job = {"command": "read_rom", "core": "nes",
               "request_id": "rad-pal", "output":
               os.path.join(self.temp.name, "rad-racer-eu.nes"),
               "selection": {"version": 1, "schema_id": "nes-layout-v1",
                             "mode": "layout", "layout_id": eu[0]["layout_id"]}}
        auto = dict(job, output=os.path.join(self.temp.name, "rad-ambiguous.nes"),
                    selection={"version": 1, "schema_id": "nes-layout-v1", "mode": "auto"})
        events = self.run_cli(1, [auto], dict(env, RETRODE_MOCK_NES_WRITELOG=log))
        self.assertEqual(events[-1]["event"], "error", events)
        self.assertFalse(os.path.exists(log))
        events = self.run_cli(1, [job], env)
        result = events[-1]
        self.assertEqual(result["event"], "result", events)
        self.assertTrue(result["verified"], events)
        self.assertEqual(result["identity"]["title"], "Rad Racer (Europe)")
        self.assertEqual(result["verification"]["actual"], "654F4E90")
        self.assertEqual(result["bytes_written"], 131088)
        with io.open(job["output"], "rb") as handle:
            data = handle.read()
        self.assertEqual(zlib.crc32(data[16:]) & 0xffffffff, 0x654F4E90)
        self.assertEqual(data[12], 1)  # PAL region bit in NES 2.0

        # Explicitly picking the misleading JP candidate must be reported
        # as a mismatched identity, not silently considered verified.
        mismatch = dict(job, request_id="rad-jp-wrong",
                        output=os.path.join(self.temp.name, "rad-wrong.nes"),
                        selection={"version": 1, "schema_id": "nes-layout-v1",
                                   "mode": "candidate", "candidate_id": jp[0]["id"]})
        events = self.run_cli(1, [mismatch], env)
        self.assertEqual(events[-1]["verification"]["status"], "mismatch")
        self.assertFalse(events[-1]["verified"])
        self.assertEqual(events[-1]["identity"]["title"], "Rad Racer (Europe)")
        self.assertEqual(events[-1]["verification"]["expected"], "02589598")

    def test_v3_mario_and_yoshi_europe_detects_and_verifies_auto(self):
        # Five identical live $8000/$E000 CRCs plus an actual PRG+CHR dump
        # prove the cartridge is readable and establish stable fingerprints.
        # Synthetic ROM bytes reproduce the two probe CRCs and total CRC.
        env = {"RETRODE_MOCK_NES_PRG_KIB": "128",
               "RETRODE_MOCK_NES_CHR_KIB": "32",
               "RETRODE_MOCK_NES_MARIO_YOSHI_EU": "1"}
        probe = {"command": "probe", "core": "nes", "request_id": "yoshi-probe"}
        read_log = os.path.join(self.temp.name, "yoshi-probe-writes")
        events = self.run_cli(1, [probe],
                              dict(env, RETRODE_MOCK_NES_WRITELOG=read_log))
        self.assertEqual(events[-1]["event"], "probe_result", events)
        info = events[-1]
        self.assertEqual(info["probe_fingerprints"]["cpu_8000_crc32"],
                         "29D1D575")
        self.assertEqual(info["probe_fingerprints"]["cpu_e000_crc32"],
                         "9A78A49D")
        self.assertFalse(os.path.exists(read_log),
                         "Fast-probe must never change MMC1 registers")
        self.assertEqual(info["identification_status"], "unique", events)
        self.assertEqual(info["mapping"], "mmc1")
        self.assertFalse(info["selection_required"])
        self.assertEqual(info["ambiguity"], "none")
        self.assertEqual(len(info["candidates"]), 1)
        candidate = info["candidates"][0]
        self.assertEqual(candidate["title"], "Mario & Yoshi (Europe)")
        self.assertTrue(candidate["read_supported"])
        self.assertEqual(candidate["layout_id"], "m1-128-32")
        self.assertEqual(candidate["payload_crc32_expected"], "836685C4")
        self.assertEqual(len(info["read_layouts"]), 1)
        self.assertEqual(info["read_layouts"][0]["id"], "m1-128-32")
        self.assertEqual(info["size_bytes"], 163856)
        job = {"command": "read_rom", "core": "nes",
               "request_id": "yoshi-eu",
               "output": os.path.join(self.temp.name, "mario-yoshi.nes"),
               "selection": {"version": 1, "schema_id": "nes-layout-v1",
                             "mode": "auto"}}
        events = self.run_cli(1, [job], env)
        result = events[-1]
        self.assertEqual(result["event"], "result", events)
        self.assertTrue(result["verified"], events)
        self.assertEqual(result["identity"]["title"], "Mario & Yoshi (Europe)")
        self.assertEqual(result["identity"]["matched_candidate_id"],
                         candidate["id"])
        self.assertEqual(result["verification"]["actual"], "836685C4")
        self.assertEqual(result["read_configuration"]["mapper"], 1)
        self.assertEqual(result["read_configuration"]["prg_kib"], 128)
        self.assertEqual(result["read_configuration"]["chr_kib"], 32)
        self.assertEqual(result["bytes_written"], 163856)
        with io.open(job["output"], "rb") as handle:
            data = handle.read()
        self.assertEqual(data[12], 1)  # PAL
        self.assertEqual(zlib.crc32(data[16:]) & 0xffffffff, 0x836685C4)

    def test_v3_mario_and_yoshi_manual_override_still_verifies(self):
        env = {"RETRODE_MOCK_NES_PRG_KIB": "128",
               "RETRODE_MOCK_NES_CHR_KIB": "32",
               "RETRODE_MOCK_NES_MARIO_YOSHI_EU": "1"}
        job = {"command": "read_rom", "core": "nes",
               "request_id": "yoshi-manual",
               "output": os.path.join(self.temp.name, "mario-yoshi-manual.nes"),
               "selection": self.v3_override(128, 32)}
        events = self.run_cli(1, [job], env)
        self.assertEqual(events[-1]["event"], "result", events)
        self.assertTrue(events[-1]["verified"], events)
        self.assertEqual(events[-1]["verification"]["actual"], "836685C4")

    def test_v3_faxanadu_europe_known_crc_without_probe_match(self):
        env = {"RETRODE_MOCK_NES_PRG_KIB": "256",
               "RETRODE_MOCK_NES_CHR_KIB": "0",
               "RETRODE_MOCK_NES_FAXANADU_EU": "1"}
        probe = {"command": "probe", "core": "nes", "request_id": "fax-probe"}
        events = self.run_cli(1, [probe], env)
        self.assertEqual(events[-1]["event"], "probe_result", events)
        self.assertIn("probe_fingerprints", events[-1])
        self.assertEqual(events[-1]["identification_status"], "unknown")
        job = {"command": "read_rom", "core": "nes",
               "request_id": "fax-eu",
               "output": os.path.join(self.temp.name, "faxanadu.nes"),
               "selection": self.v3_override(256, 0)}
        events = self.run_cli(1, [job], env)
        result = events[-1]
        self.assertEqual(result["event"], "result", events)
        self.assertTrue(result["verified"], events)
        self.assertEqual(result["identity"]["title"], "Faxanadu (Europe)")
        self.assertEqual(result["verification"]["actual"], "76C161E3")
        self.assertEqual(result["bytes_written"], 262160)
        with io.open(job["output"], "rb") as handle:
            data = handle.read()
        self.assertEqual(zlib.crc32(data[16:]) & 0xffffffff, 0x76C161E3)

    def test_mmc1_in_regular_production_core_no_lab_switch(self):
        # This .so is compiled WITHOUT RETRODE3_TESTING, MMC1_LAB or
        # any experimental mapper-writes build flags.
        shutil.copyfile(PRODUCTION_MODULE, os.path.join(self.available, "nes.so"))
        job = self.cmd(1, 128, 32, "normal-core-mmc1")
        events = self.run_cli(1, [job], {
            "RETRODE_MOCK_NES_PRG_KIB": "128",
            "RETRODE_MOCK_NES_CHR_KIB": "32"})
        self.assertEqual(events[-1]["event"], "result", events)
        self.assertEqual(events[-1]["layout_id"], "m1-128-32")
        with io.open(job["output"], "rb") as handle:
            self.assertEqual(handle.read(), fixture_rom(1, 128, 32))

        # The regular development binary also supports all four new
        # mappers through v3; the same bank/CRC tests use it below.
        cap = self.run_cli(2, [{"command": "get_capabilities", "core": "nes",
                               "request_id": "cap"}])[-1]
        self.assertEqual(cap["event"], "capabilities")
        schema = cap["selection_schema"]
        field = next(f for f in schema["fields"] if f["id"] == "mapper")
        self.assertEqual(set(v["value"] for v in field["values"]
                             if v["read_supported"]), {0, 1, 2, 3, 4, 7, 9, 10, 11, 13, 64, 66, 69, 71, 76, 88, 95, 118, 119, 154, 158, 206, 268})

    def test_bank_order_and_crc_for_mappers_2_3_4_7(self):
        shutil.copyfile(PRODUCTION_MODULE, os.path.join(self.available, "nes.so"))
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

    def test_v3_new_mapper_overrides(self):
        shutil.copyfile(PRODUCTION_MODULE, os.path.join(self.available, "nes.so"))
        for mapper, p, c in ((2, 64, 0), (3, 32, 32),
                             (4, 128, 64), (7, 128, 0)):
            selection = self.v3_override(p, c)
            selection["parameters"]["mapper"] = mapper
            output = os.path.join(self.temp.name, "v3-m%s.nes" % mapper)
            events = self.run_cli(mapper, [{"command": "read_rom", "core": "nes",
                 "request_id": "v3-m%s" % mapper, "output": output,
                 "selection": selection}])
            self.assertEqual(events[-1]["event"], "result", events)
            self.assertFalse(events[-1]["verified"])
            with io.open(output, "rb") as handle:
                self.assertEqual(handle.read(), fixture_rom(mapper, p, c))

    def test_generated_latch_and_dxrom_families(self):
        # The production build runs ORIGINAL generated bank loops.
        shutil.copyfile(PRODUCTION_MODULE, os.path.join(self.available, "nes.so"))
        for mapper, p, c, expected_writes in (
                (11, 128, 32, 8), (13, 32, 0, 0),
                (66, 128, 32, 8), (71, 128, 0, 8),
                (206, 128, 64, 160), (206, 32, 32, 64)):
            name = "generated-%d-%d" % (mapper, p)
            selection = self.v3_override(p, c)
            selection["parameters"]["mapper"] = mapper
            output = os.path.join(self.temp.name, name + ".nes")
            log = os.path.join(self.temp.name, name + ".writes")
            env = {"RETRODE_MOCK_NES_PRG_KIB": str(p),
                   "RETRODE_MOCK_NES_CHR_KIB": str(c),
                   "RETRODE_MOCK_NES_WRITELOG": log}
            events = self.run_cli(mapper, [{"command": "probe", "core": "nes",
                                           "request_id": name + "-probe"}], env)
            self.assertEqual(events[-1]["event"], "probe_result", events)
            self.assertFalse(os.path.exists(log))
            events = self.run_cli(mapper, [{"command": "read_rom", "core": "nes",
                "request_id": name, "output": output, "selection": selection}], env)
            self.assertEqual(events[-1]["event"], "result", (mapper, events))
            with io.open(output, "rb") as handle:
                self.assertEqual(handle.read(), fixture_rom(mapper, p, c))
            if expected_writes:
                with io.open(log, "r", encoding="utf-8") as handle:
                    writes = [tuple(int(v) for v in line.split()) for line in handle]
                self.assertEqual(len(writes), expected_writes, (mapper, writes))
                self.assertTrue(all(addr >= 0x8000 and addr != 0xa001 and mode == 10
                                    for addr, value, mode in writes))
            else:
                self.assertFalse(os.path.exists(log))

    def test_reviewed_register_families(self):
        shutil.copyfile(PRODUCTION_MODULE, os.path.join(self.available, "nes.so"))
        for mapper, p, c in ((9, 128, 128), (10, 256, 128),
                             (64, 128, 256), (69, 256, 256),
                             (76, 128, 128), (88, 128, 128),
                             (95, 128, 32), (118, 256, 128),
                             (119, 128, 64), (154, 128, 128),
                             (158, 128, 128)):
            name = "family-%d" % mapper
            selection = self.v3_override(p, c)
            selection["parameters"]["mapper"] = mapper
            output = os.path.join(self.temp.name, name + ".nes")
            log = os.path.join(self.temp.name, name + ".writes")
            env = {"RETRODE_MOCK_NES_PRG_KIB": str(p),
                   "RETRODE_MOCK_NES_CHR_KIB": str(c),
                   "RETRODE_MOCK_NES_WRITELOG": log}
            events = self.run_cli(mapper, [{"command": "probe", "core": "nes",
                                           "request_id": name + "-probe"}], env)
            self.assertEqual(events[-1]["event"], "probe_result", events)
            self.assertFalse(os.path.exists(log))
            job = {"command": "read_rom", "core": "nes", "request_id": name,
                   "output": output, "selection": selection}
            events = self.run_cli(mapper, [job], env)
            self.assertEqual(events[-1]["event"], "result", (mapper, events))
            with io.open(output, "rb") as handle:
                actual = handle.read()
            expected = fixture_rom(mapper, p, c)
            self.assertEqual(actual[16:], expected[16:], mapper)
            if mapper == 119:
                self.assertEqual(actual[7] & 12, 8)
                self.assertEqual(actual[11], 7)
            else:
                self.assertEqual(actual[:16], expected[:16], mapper)
            with io.open(log, "r", encoding="utf-8") as handle:
                writes = [tuple(int(v) for v in line.split()) for line in handle]
            self.assertTrue(writes)
            self.assertTrue(all(addr >= 0x8000 and addr != 0xa001 and mode == 10
                                for addr, value, mode in writes))
            os.unlink(log)
            selection["parameters"]["battery"] = True
            job["output"] += ".blocked"
            events = self.run_cli(mapper, [job], env)
            self.assertEqual(events[-1]["event"], "error", (mapper, events))
            self.assertFalse(os.path.exists(log))

    def test_smd133_outer_register_windows_and_nes2_header(self):
        shutil.copyfile(PRODUCTION_MODULE, os.path.join(self.available, "nes.so"))
        for sub, window in ((0, 0x6000), (1, 0x5000)):
            selection = self.v3_override(512, 0)
            selection["parameters"].update(mapper=268, submapper=sub)
            name = "smd133-%d" % sub
            output = os.path.join(self.temp.name, name + ".nes")
            log = os.path.join(self.temp.name, name + ".writes")
            env = {"RETRODE_MOCK_NES_SUBMAPPER": str(sub),
                   "RETRODE_MOCK_NES_WRITELOG": log}
            probe = self.run_cli(268, [{"command": "probe", "core": "nes",
                                        "request_id": name + "-probe"}], env)[-1]
            self.assertEqual(probe["event"], "probe_result", probe)
            self.assertFalse(os.path.exists(log))
            auto = {"version": 1, "schema_id": "nes-layout-v1", "mode": "auto"}
            job = {"command": "read_rom", "core": "nes", "request_id": name,
                   "output": output, "selection": auto}
            events = self.run_cli(268, [job], env)
            self.assertEqual(events[-1]["event"], "error", events)
            self.assertFalse(os.path.exists(log))
            job["selection"] = selection
            events = self.run_cli(268, [job], env)
            result = events[-1]
            self.assertEqual(result["event"], "result", events)
            self.assertFalse(result["verified"])
            self.assertEqual(result["bytes_written"], 524304)
            self.assertEqual(result["read_configuration"]["mapper"], 268)
            self.assertEqual(result["read_configuration"]["submapper"], sub)
            with io.open(output, "rb") as handle:
                actual = handle.read()
            self.assertEqual(actual[:16], bytes(bytearray(
                [0x4e, 0x45, 0x53, 0x1a, 32, 0, 0xc0, 0x08,
                 (sub << 4) | 1, 0, 0, 12, 0, 0, 0, 0])))
            # Same synthetic PRG payload as the other mapper fixtures.
            self.assertEqual(actual[16:], fixture_rom(268, 512, 0)[16:])
            self.assertEqual(result["verification"]["actual"],
                             "%08X" % (zlib.crc32(actual[16:]) & 0xffffffff))
            with io.open(log, encoding="utf-8") as handle:
                writes = [tuple(int(v) for v in line.split()) for line in handle]
            expected = [(0xa001, 0, 10)]
            for bank in range(64):
                expected.extend([(window, bank >> 4, 10), (window + 1, 0x60, 10),
                                 (window + 2, 0, 10), (window + 3, 0, 10),
                                 (0x8000, 6, 10), (0x8001, bank, 10)])
            self.assertEqual(writes, expected)

    def test_metal_storm_detects_and_dumps_with_auto_selection(self):
        shutil.copyfile(PRODUCTION_MODULE, os.path.join(self.available, "nes.so"))
        log = os.path.join(self.temp.name, "metal-storm.writes")
        env = {"RETRODE_MOCK_NES_METAL_STORM": "1",
               "RETRODE_MOCK_NES_WRITELOG": log}
        probe = {"command": "probe", "core": "nes", "request_id": "metal-probe"}
        info = self.run_cli(268, [probe], env)[-1]
        self.assertEqual(info["event"], "probe_result", info)
        self.assertEqual(info["title"], "Metal Storm (World) (Retro-Bit)")
        self.assertEqual(info["mapping"], "smd133")
        self.assertEqual(info["probe_fingerprints"], {
            "cpu_8000_crc32": "0E94BBA9", "cpu_e000_crc32": "B2AA7578"})
        self.assertFalse(info["selection_required"])
        self.assertEqual(info["size_bytes"], 524304)
        self.assertTrue(info["candidates"][0]["read_supported"])
        self.assertEqual(info["candidates"][0]["payload_crc32_expected"], "1BE6552C")
        self.assertFalse(os.path.exists(log))
        bad = self.run_cli(268, [probe], dict(env, RETRODE_MOCK_NES_METAL_STORM_BAD_PROBE="1"))[-1]
        self.assertNotEqual(bad["title"], info["title"])
        self.assertFalse(os.path.exists(log))
        job = {"command": "read_rom", "core": "nes", "request_id": "metal-auto",
               "output": os.path.join(self.temp.name, "metal.nes"),
               "selection": {"version": 1, "schema_id": "nes-layout-v1", "mode": "auto"}}
        result = self.run_cli(268, [job], env)[-1]
        self.assertEqual(result["event"], "result", result)
        self.assertTrue(result["verified"], result)
        self.assertEqual(result["identity"]["title"], info["title"])
        self.assertEqual(result["verification"]["expected"], "1BE6552C")
        self.assertEqual(result["verification"]["actual"], "1BE6552C")
        with io.open(job["output"], "rb") as handle:
            data = handle.read()
        self.assertEqual(len(data), 524304)
        self.assertEqual(data[:16], bytes(bytearray(
            [0x4e, 0x45, 0x53, 0x1a, 32, 0, 0xc0, 0x08,
             1, 0, 0, 12, 0, 0, 0, 1])))
        self.assertEqual(zlib.crc32(data[16:]) & 0xffffffff, 0x1BE6552C)
        with io.open(log, encoding="utf-8") as handle:
            writes = [tuple(int(v) for v in line.split()) for line in handle]
        expected = [(0xa001, 0x80, 10)]
        for bank in range(64):
            expected.extend([(0x5000, bank >> 4, 10), (0x5001, 0x60, 10),
                             (0x6000, bank >> 4, 10), (0x6001, 0x60, 10),
                             (0x5002, 0, 10), (0x5003, 0, 10),
                             (0x6002, 0, 10), (0x6003, 0, 10),
                             (0x8000, 6, 10), (0x8001, bank, 10)])
        self.assertEqual(writes, expected)
        job["output"] = os.path.join(self.temp.name, "metal-bad.nes")
        bad = self.run_cli(268, [job], dict(env, RETRODE_MOCK_NES_METAL_STORM_BAD_CRC="1"))[-1]
        self.assertEqual(bad["event"], "result", bad)
        self.assertFalse(bad["verified"])
        self.assertEqual(bad["verification"]["status"], "mismatch")

    def test_smd133_invalid_profile_rejected_before_writes(self):
        for i, patch in enumerate(({"submapper": 2}, {"prg_kib": 256},
                                   {"chr_kib": 8}, {"battery": True})):
            selection = self.v3_override(512, 0)
            selection["parameters"].update(mapper=268)
            selection["parameters"].update(patch)
            output = os.path.join(self.temp.name, "bad-smd-%d.nes" % i)
            log = os.path.join(self.temp.name, "bad-smd-%d.log" % i)
            events = self.run_cli(268, [{"command": "read_rom", "core": "nes",
                "request_id": "bad-smd", "output": output, "selection": selection}],
                {"RETRODE_MOCK_NES_WRITELOG": log})
            self.assertEqual(events[-1]["event"], "error", events)
            self.assertFalse(os.path.exists(output))
            self.assertFalse(os.path.exists(log))

    def test_constant_last_window_is_not_a_fingerprint_match(self):
        log = os.path.join(self.temp.name, "constant-window-writes")
        events = self.run_cli(4, [{"command": "probe", "core": "nes",
                                   "request_id": "constant-window"}],
                              {"RETRODE_MOCK_NES_CONSTANT_LAST": "1",
                               "RETRODE_MOCK_NES_WRITELOG": log})
        probe = events[-1]
        self.assertEqual(probe["event"], "probe_result", events)
        self.assertEqual(probe["probe_fingerprints"]["cpu_e000_crc32"],
                         "B2AA7578")
        false_titles = {"Break Time - The National Pool Tour (USA)",
                        "Goal! (USA)", "3-D WorldRunner (USA)", "Tetris 2 (USA)"}
        self.assertFalse(false_titles.intersection(
            c["title"] for c in probe["candidates"]))
        self.assertFalse(os.path.exists(log))

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

    def test_mmc1_manual_override_cannot_bypass_known_battery_fingerprint(self):
        # Synthetic first-window CRC A24E2D45 is the pinned MMC1 entry
        # for A Ressha de Ikou, which has battery-backed PRG-RAM.
        # Deliberately lie with battery=0: no register writes are allowed.
        job = self.cmd(1, 128, 16, "mmc1-known-battery")
        write_log = os.path.join(self.temp.name, "mmc1-known-battery-writes")
        probe = {"command": "probe", "core": "nes", "request_id": "battery-probe"}
        env = {"RETRODE_MOCK_NES_PRG_KIB": "128",
               "RETRODE_MOCK_NES_CHR_KIB": "16",
               "RETRODE_MOCK_NES_BATTERY_PROBE": "1",
               "RETRODE_MOCK_NES_WRITELOG": write_log}
        events = self.run_cli(1, [probe], env)
        self.assertEqual(events[-1]["event"], "probe_result", events)
        match = [c for c in events[-1]["candidates"]
                 if c["title"] == "A Ressha de Ikou (Japan)"]
        self.assertEqual(len(match), 1, events)
        self.assertFalse(match[0]["read_supported"])
        events = self.run_cli(1, [job], env)
        self.assertEqual(events[-1]["event"], "error", events)
        self.assertIn("battery-backed fingerprint", events[-1]["message"])
        self.assertFalse(os.path.exists(job["output"]))
        self.assertFalse(os.path.exists(write_log))

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
        for mapper, p, c in ((2, 64, 0), (3, 32, 32), (7, 128, 0)):
            job = self.cmd(mapper, p, c, "no-safe-%s" % mapper)
            log = os.path.join(self.temp.name, "no-safe-writes-%s" % mapper)
            events = self.run_cli(mapper, [job],
                         {"RETRODE_MOCK_NES_NO_CONFLICT_SAFE": "1",
                          "RETRODE_MOCK_NES_WRITELOG": log})
            self.assertEqual(events[-1]["event"], "error", events)
            self.assertIn("bus-conflict-safe", events[-1]["message"])
            self.assertFalse(os.path.exists(job["output"]))
            self.assertFalse(os.path.exists(log))

    def test_new_mapper_battery_layouts_rejected_before_write(self):
        shutil.copyfile(PRODUCTION_MODULE, os.path.join(self.available, "nes.so"))
        for mapper, p, c in ((2, 64, 0), (3, 32, 32),
                             (4, 128, 64), (7, 128, 0)):
            for v3 in (False, True):
                name = "battery-%s-%s" % (mapper, v3)
                log = os.path.join(self.temp.name, name + ".log")
                job = self.cmd(mapper, p, c, name)
                job["battery"] = 1
                if v3:
                    for key in ("mapper", "submapper", "prg_kib", "chr_kib",
                                "mirroring", "battery"):
                        del job[key]
                    job["selection"] = self.v3_override(p, c)
                    job["selection"]["parameters"].update(mapper=mapper, battery=True)
                events = self.run_cli(mapper, [job], {"RETRODE_MOCK_NES_WRITELOG": log})
                self.assertEqual(events[-1]["event"], "error", events)
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
                             (2, 64, 0, 3)):
            job = self.cmd(m, p, c, "invalid-%s-%s" % (m, sub))
            job["submapper"] = sub
            events = self.run_cli(m, [job])
            self.assertEqual(events[-1]["event"], "error", events)
            self.assertFalse(os.path.exists(job["output"]))


if __name__ == "__main__":
    unittest.main()
