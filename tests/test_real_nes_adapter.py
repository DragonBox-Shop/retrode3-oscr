"""Pinned Sanni NES mapper-0 bank loops and shared host tests (Python 3.4)."""
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
GENERATED = os.path.join(ROOT, "build/migration/oscr_nes_nrom.cpp")

def expected_rom():
    header = bytes(bytearray.fromhex("4E45531A010101080000000001000020"))
    prg = bytes(bytearray((i * 7 + (i >> 8)) & 255 for i in range(16384)))
    chr_data = bytes(bytearray((i * 11 + 3) & 255 for i in range(8192)))
    return header + prg + chr_data


class TestRealNesAdapter(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.temp.name, "available")
        self.enabled = os.path.join(self.temp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        shutil.copyfile(MODULE, os.path.join(self.available, "nes.so"))
        shutil.copyfile(MANIFEST, os.path.join(self.available, "nes.core"))

    def tearDown(self):
        self.temp.cleanup()

    def enable(self):
        os.symlink(os.path.join(self.available, "nes.so"),
                   os.path.join(self.enabled, "nes.so"))

    def host(self, commands, env=None):
        args = [HOST, "--protocol", "ndjson-v1", "--interactive",
                "--available-dir", self.available, "--enabled-dir", self.enabled]
        process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, env=env)
        stdin = "".join(json.dumps(command) + "\n" for command in commands).encode("utf-8")
        stdout, stderr = process.communicate(stdin)
        return (process.returncode,
                [json.loads(line) for line in stdout.decode("utf-8").splitlines()],
                stderr.decode("utf-8", "replace"))

    def test_original_sanni_bank_order_and_complete_ines_output(self):
        self.enable()
        target = os.path.join(self.temp.name, "mock.nes")
        rc, events, errors = self.host([
            {"command": "list_cores", "request_id": "list"},
            {"command": "get_capabilities", "core": "nes", "request_id": "cap"},
            {"command": "probe", "core": "nes", "request_id": "probe"},
            {"command": "read_rom", "core": "nes", "request_id": "read",
             "output": target}
        ])
        self.assertEqual(rc, 0, errors)
        core = events[1]["cores"][0]
        self.assertEqual(core["id"], "nes")
        self.assertTrue(core["usable"])
        self.assertEqual(core["status"], "ready")
        self.assertEqual(core["actions"], ["probe", "read_rom"])
        self.assertEqual(events[2]["event"], "capabilities")
        self.assertFalse(events[2]["write_operations"])
        self.assertEqual(events[3]["event"], "probe_result", events)
        self.assertEqual(events[3]["mapping"], "nrom")
        self.assertEqual(events[3]["title"], "MOCK NES NROM")
        self.assertIsNone(events[3]["checksum"])
        self.assertEqual(events[3]["identification_status"], "unique")
        self.assertEqual(events[3]["read_layouts"][0]["id"], "nrom-16-8")
        self.assertEqual(events[3]["size_bytes"], 24592)
        progress = [event for event in events if event["event"] == "progress"]
        self.assertGreater(len(progress), 2)
        self.assertEqual(progress[0]["bytes_done"], 0)
        self.assertEqual(progress[-1]["bytes_done"], 24592)
        self.assertEqual(progress[-1]["percent"], 100)
        self.assertTrue(all(a["bytes_done"] <= b["bytes_done"]
                            for a, b in zip(progress, progress[1:])))
        self.assertEqual(events[-1]["event"], "result")
        self.assertEqual(events[-1]["bytes_written"], 24592)
        with io.open(target, "rb") as handle:
            actual = handle.read()
        self.assertEqual(actual, expected_rom())
        self.assertEqual("%08X" % (zlib.crc32(actual[16:]) & 0xffffffff), "6124E914")
        self.assertEqual(events[-1]["crc32"],
                         "%08X" % (zlib.crc32(actual) & 0xffffffff))
        self.assertTrue(events[-1]["verified"])
        self.assertEqual(events[-1]["payload_crc32"], "6124E914")

    def test_nrom_16_and_32_kib_prg_with_optional_chr(self):
        self.enable()
        for variant, prg_kib, chr_kib, expected_crc, title, header_hex in (
            ("16-0", 16, 0, "716A9A25", "MOCK NES NROM 16-0",
             "4E45531A010001080000000000000001"),
            ("32-0", 32, 0, "9A4D4BA5", "MOCK NES NROM 32-0",
             "4E45531A020001080000000000000001"),
            ("32-8", 32, 8, "3F59F616", "MOCK NES NROM 32-8",
             "4E45531A020101080000000000000001"),
        ):
            output = os.path.join(self.temp.name, variant + ".nes")
            env = os.environ.copy()
            env["RETRODE_MOCK_NES_SIZE"] = variant
            rc, events, errors = self.host([
                {"command": "probe", "core": "nes", "request_id": "probe"},
                {"command": "read_rom", "core": "nes", "request_id": "read",
                 "output": output}
            ], env=env)
            self.assertEqual(rc, 0, (variant, errors))
            self.assertEqual(events[1]["event"], "probe_result")
            self.assertEqual(events[1]["title"], title)
            self.assertIsNone(events[1]["checksum"])
            self.assertEqual(events[1]["identification_status"], "unique")
            self.assertEqual(events[1]["size_bytes"], 16 + (prg_kib + chr_kib) * 1024)
            self.assertEqual(events[-1]["event"], "result")
            with io.open(output, "rb") as handle:
                actual = handle.read()
            header = bytes(bytearray.fromhex(header_hex))
            multiplier, bias = {
                "16-0": (9, 1), "32-0": (17, 3), "32-8": (13, 2)
            }[variant]
            prg = bytes(bytearray((i * multiplier + (i >> 8) + bias) & 255
                                  for i in range(prg_kib * 1024)))
            chr_data = bytes(bytearray((i * 11 + 3) & 255
                                       for i in range(chr_kib * 1024)))
            self.assertEqual(actual, header + prg + chr_data)
            self.assertEqual("%08X" % (zlib.crc32(actual[16:]) & 0xffffffff),
                             expected_crc)
            self.assertEqual(events[-1]["crc32"],
                             "%08X" % (zlib.crc32(actual) & 0xffffffff))

    def test_one_shot_original_mapper_zero_probe(self):
        self.enable()
        args = [HOST, "--protocol", "ndjson-v1", "--core", "nes",
                "--action", "probe",
                "--available-dir", self.available, "--enabled-dir", self.enabled]
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output, error = process.communicate()
        self.assertEqual(process.returncode, 0, (error, output))
        event = json.loads(output.decode("utf-8"))
        self.assertEqual(event["event"], "probe_result")
        self.assertEqual(event["mapping"], "nrom")
        self.assertEqual(event["size_bytes"], 24592)

    def test_snes_and_nes_are_discovered_in_the_same_shared_cli(self):
        self.enable()
        # The existing SNES test module needs no cartridge hardware.
        fixture = os.path.join(ROOT, "build/migration/registry-fixtures/snes.so")
        shutil.copyfile(fixture, os.path.join(self.available, "snes.so"))
        with io.open(os.path.join(self.available, "snes.core"), "w",
                     encoding="utf-8") as handle:
            handle.write("id=snes\nname=Super Nintendo\n"
                         "version=0.1.0-test\nslot=snes\n")
        os.symlink(os.path.join(self.available, "snes.so"),
                   os.path.join(self.enabled, "snes.so"))
        output = os.path.join(self.temp.name, "both-nes.nes")
        rc, events, errors = self.host([
            {"command": "list_cores", "request_id": "list"},
            {"command": "probe", "core": "nes", "request_id": "nes-probe"},
            {"command": "probe", "core": "snes", "request_id": "snes-probe"},
            {"command": "read_rom", "core": "nes", "request_id": "nes-read",
             "output": output}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertEqual([x["id"] for x in events[1]["cores"]], ["nes", "snes"])
        self.assertTrue(all(x["usable"] for x in events[1]["cores"]))
        probes = [x for x in events if x["event"] == "probe_result"]
        self.assertEqual([(x["system"], x["mapping"]) for x in probes],
                         [("nes", "nrom"), ("snes", "lo")])
        self.assertEqual(events[-1]["event"], "result")
        with io.open(output, "rb") as handle:
            self.assertEqual(handle.read(), expected_rom())

    def test_disabled_nes_module_not_usable(self):
        rc, events, errors = self.host([
            {"command": "list_cores", "request_id": "list"},
            {"command": "probe", "core": "nes", "request_id": "probe"}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertFalse(events[1]["cores"][0]["usable"])
        self.assertEqual(events[1]["cores"][0]["status"], "disabled")
        self.assertEqual(events[-1]["event"], "error")

    def test_unknown_or_empty_carts_fail_without_rom_output(self):
        self.enable()
        for bad in ("unknown", "empty"):
            output = os.path.join(self.temp.name, bad + ".nes")
            env = os.environ.copy()
            env["RETRODE_MOCK_NES_BAD"] = bad
            rc, events, errors = self.host([
                {"command": "read_rom", "core": "nes", "request_id": bad,
                 "output": output}
            ], env=env)
            self.assertEqual(rc, 0, errors)
            self.assertEqual(events[-1]["event"], "error")
            self.assertFalse(os.path.exists(output))
            self.assertFalse(any(name.startswith(bad + ".nes.partial-")
                                 for name in os.listdir(self.temp.name)))

    def test_probe_reads_only_1_kib_and_dump_reads_payload_once(self):
        self.enable()
        output = os.path.join(self.temp.name, "bounded.nes")
        log = os.path.join(self.temp.name, "nes-readlog.txt")
        env = os.environ.copy()
        env["RETRODE_MOCK_NES_READLOG"] = log
        rc, events, errors = self.host([
            {"command": "probe", "core": "nes", "request_id": "probe"}
        ], env=env)
        self.assertEqual(rc, 0, errors)
        with io.open(log, "r", encoding="utf-8") as handle:
            calls = [int(line.split()[1]) for line in handle if line.strip()]
        self.assertEqual(sum(calls), 1024)
        with io.open(log, "w", encoding="utf-8") as handle:
            handle.write("")
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "nes", "request_id": "read",
             "output": output}
        ], env=env)
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[-1]["event"], "result", events)
        with io.open(log, "r", encoding="utf-8") as handle:
            calls = [int(line.split()[1]) for line in handle if line.strip()]
        self.assertEqual(sum(calls), 1024 + 16384 + 8192)
        self.assertTrue(events[-1]["verified"])

    def test_unknown_homebrew_manual_override(self):
        self.enable()
        output = os.path.join(self.temp.name, "homebrew.nes")
        env = os.environ.copy()
        env["RETRODE_MOCK_NES_BAD"] = "unknown"
        rc, events, errors = self.host([
            {"command": "probe", "core": "nes", "request_id": "probe"},
            {"command": "read_rom", "core": "nes", "request_id": "read",
             "output": output,
             "mapper": 0, "submapper": 0, "prg_kib": 16, "chr_kib": 8,
             "mirroring": "horizontal", "battery": 0}
        ], env=env)
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[1]["identification_status"], "unknown")
        self.assertEqual(events[-1]["event"], "result", events)
        self.assertFalse(events[-1]["verified"])
        with io.open(output, "rb") as handle:
            rom = handle.read()
        self.assertEqual(len(rom), 24592)
        self.assertEqual(rom[:16], bytes(bytearray.fromhex(
            "4E45531A010100000000000000000000")))

    def test_reject_unsupported_mapper_and_incomplete_manual_fields(self):
        self.enable()
        for request in (
            {"mapper": 4, "submapper": 0, "prg_kib": 16, "chr_kib": 8,
             "mirroring": "horizontal", "battery": 0},
            {"mapper": 0, "prg_kib": 16, "chr_kib": 8},
            {"candidate_id": "fake", "layout_id": "nrom-16-8"},
        ):
            command = dict(request)
            command.update({"command": "read_rom", "core": "nes",
                            "request_id": "invalid",
                            "output": os.path.join(self.temp.name, "invalid.nes")})
            rc, events, errors = self.host([command])
            self.assertEqual(rc, 0, errors)
            self.assertEqual(events[-1]["event"], "error", events)
            self.assertFalse(os.path.exists(command["output"]))

    def test_cancel_cleans_partial_output(self):
        self.enable()
        output = os.path.join(self.temp.name, "cancel.nes")
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "nes", "request_id": "job",
             "output": output},
            {"command": "cancel", "request_id": "job"}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertTrue(any(event.get("code") == "cancelled" for event in events))
        self.assertFalse(os.path.exists(output))
        self.assertFalse(any(name.startswith("cancel.nes.partial-")
                             for name in os.listdir(self.temp.name)))

    def test_existing_rom_not_overwritten(self):
        self.enable()
        output = os.path.join(self.temp.name, "existing.nes")
        with io.open(output, "wb") as handle:
            handle.write(b"KEEP")
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "nes", "request_id": "keep",
             "output": output}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[-1]["event"], "error")
        with io.open(output, "rb") as handle:
            self.assertEqual(handle.read(), b"KEEP")

    def test_generator_original_sanni_loops_and_no_unreviewed_writes(self):
        with io.open(GENERATED, "r", encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("void dumpBankPRG(", source)
        self.assertIn("void dumpBankCHR(", source)
        self.assertIn("address += 512", source)
        self.assertIn("OSCR::RetroDe3::readPRGBlock(", source)
        self.assertIn("OSCR::RetroDe3::readCHRBlock(", source)
        # Low-level block iterators must remain strictly read-only.
        # The complete upstream mapper switches now contain register calls,
        # but must not bypass the separately guarded HAL.
        blocks = source.split("  void write_mmc1_byte(", 1)[0]
        for forbidden in ("write_prg_byte(", "write_reg_byte(", "NES_MAPPER"):
            self.assertNotIn(forbidden, blocks)
        for forbidden in ("nes_write(", "nes_read(", "writeRAM(", "retrode3-lib.h"):
            self.assertNotIn(forbidden, source)
        self.assertIn("void generatedReadPRG(", source)
        self.assertIn("void generatedReadCHR(", source)
        self.assertIn("case 268:", source)

    def test_strict_upstream_guard_rejects_modified_bank_iterator(self):
        original = os.path.join(ROOT, "third_party/oscr-firmware/src/cores/NES.cpp")
        with io.open(original, "r", encoding="utf-8") as handle:
            source = handle.read()
        first = source.index("  void dumpBankPRG(")
        last = source.index("  void dumpBankCHR(", first)
        changed = source[:first] + source[first:last].replace(
            "address += 512", "address += 256", 1) + source[last:]
        self.assertNotEqual(changed, source)
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "upstream.cpp")
            output = os.path.join(tmp, "generated.cpp")
            with io.open(path, "w", encoding="utf-8") as handle:
                handle.write(changed)
            process = subprocess.Popen(
                ["python3", os.path.join(ROOT, "tools/build-linux-nes-core.py"),
                 "--upstream", path, "--output", output],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            _, error = process.communicate()
            self.assertEqual(process.returncode, 2)
            self.assertIn(b"review required", error)
            self.assertFalse(os.path.exists(output))


    def test_generic_v3_capabilities_and_selection(self):
        self.enable()
        output = os.path.join(self.temp.name, "generic.nes")
        rc, events, errors = self.host([
            {"command": "get_capabilities", "core": "nes", "request_id": "cap"},
            {"command": "probe", "core": "nes", "request_id": "probe"},
            {"command": "read_rom", "core": "nes", "request_id": "read",
             "output": output, "selection": {
                 "version": 1, "schema_id": "nes-layout-v1", "mode": "candidate",
                 "candidate_id": "nrom-0226"}}
        ])
        self.assertEqual(rc, 0, errors)
        cap = [x for x in events if x["event"] == "capabilities"][0]
        self.assertEqual(cap["selection_schema"]["schema_version"], 1)
        self.assertEqual(cap["selection_schema"]["schema_id"], "nes-layout-v1")
        self.assertIn("override", cap["selection_schema"]["selection_modes"])
        probe = [x for x in events if x["event"] == "probe_result"][0]
        self.assertEqual(probe["ambiguity"], "none")
        self.assertEqual(probe["selection_schema_id"], "nes-layout-v1")
        result = events[-1]
        self.assertEqual(result["event"], "result", events)
        self.assertEqual(result["selection_mode"], "candidate")
        self.assertEqual(result["read_configuration"]["mapper"], 0)
        self.assertEqual(result["verification"]["status"], "verified")
        self.assertTrue(result["verification"]["verified"])
        with io.open(output, "rb") as handle:
            self.assertEqual(handle.read(), expected_rom())

    def test_generic_v3_manual_unknown_and_unverified(self):
        self.enable()
        output = os.path.join(self.temp.name, "unverified.nes")
        env = os.environ.copy()
        env["RETRODE_MOCK_NES_BAD"] = "unknown"
        selection = {"version": 1, "schema_id": "nes-layout-v1",
                     "mode": "override", "parameters": {
                         "mapper": 0, "submapper": 0, "prg_kib": 16,
                         "chr_kib": 8, "mirroring": "horizontal",
                         "battery": False}}
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "nes", "request_id": "manual",
             "output": output, "selection": selection}
        ], env=env)
        self.assertEqual(rc, 0, errors)
        result = events[-1]
        self.assertEqual(result["event"], "result", events)
        self.assertEqual(result["selection_mode"], "override")
        self.assertFalse(result["verified"])
        self.assertIn(result["verification"]["status"],
                      ("not_found", "mismatch"))
        self.assertTrue(os.path.exists(output))

    def test_generic_v3_rejects_invalid_selections_and_unsupported_mapper(self):
        self.enable()
        valid = {"version": 1, "schema_id": "nes-layout-v1",
                 "mode": "override", "parameters": {
                     "mapper": 0, "submapper": 0, "prg_kib": 16,
                     "chr_kib": 8, "mirroring": "horizontal",
                     "battery": False}}
        bad = [
            dict(valid, mode="auto"),
            dict(valid, mode="candidate"),
            dict(valid, schema_id="nes-layout-v999"),
            dict(valid, parameters=dict(valid["parameters"], mapper=123)),
            dict(valid, parameters=dict(valid["parameters"], prg_kib=512)),
            dict(valid, parameters=dict(valid["parameters"], unexpected=True)),
            dict(valid, parameters=dict(valid["parameters"], battery="false")),
            {"version": 1, "schema_id": "nes-layout-v1",
             "mode": "candidate", "candidate_id": "not-in-probe"},
            {"version": 1, "schema_id": "nes-layout-v1",
             "mode": "layout", "layout_id": "unsupported"},
            {"version": 2, "schema_id": "nes-layout-v1", "mode": "auto"}
        ]
        for i, sel in enumerate(bad):
            output = os.path.join(self.temp.name, "invalid-%s.nes" % i)
            rc, events, errors = self.host([
                {"command": "read_rom", "core": "nes",
                 "request_id": "invalid%s" % i, "output": output,
                 "selection": sel}
            ])
            self.assertEqual(rc, 0, (i, errors))
            self.assertEqual(events[-1]["event"], "error", (i, events))
            self.assertFalse(os.path.exists(output))
        output = os.path.join(self.temp.name, "mixed.nes")
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "nes", "request_id": "mixed",
             "output": output, "selection": valid, "mapper": 0}
        ])
        self.assertEqual(events[-1]["code"], "invalid_command")
        self.assertFalse(os.path.exists(output))

    def test_generic_v3_cancel_removes_partial_output(self):
        self.enable()
        output = os.path.join(self.temp.name, "v3-cancel.nes")
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "nes", "request_id": "job",
             "output": output,
             "selection": {"version": 1, "schema_id": "nes-layout-v1",
                           "mode": "auto"}},
            {"command": "cancel", "request_id": "job"}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertTrue(any(x.get("code") == "cancelled" for x in events), events)
        self.assertFalse(os.path.exists(output))
        self.assertFalse(any(n.startswith("v3-cancel.nes.partial-")
                             for n in os.listdir(self.temp.name)))


if __name__ == "__main__":
    unittest.main()
