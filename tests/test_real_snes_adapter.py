"""Actual pinned OSCR SNES C++ functions behind the shared core ABI (Python 3.4)."""
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
ORIGINAL_RUNNER = os.path.join(ROOT, "build/migration/oscr-snes-native-mock")
SNES_MODULE = os.path.join(ROOT, "build/migration/real-snes-fixture/snes.so")


class TestRealSnesAdapter(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.temp.name, "available")
        self.enabled = os.path.join(self.temp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        shutil.copyfile(SNES_MODULE, os.path.join(self.available, "snes.so"))
        shutil.copyfile(os.path.join(ROOT, "packaging/cores/snes.core"),
                        os.path.join(self.available, "snes.core"))

    def tearDown(self):
        self.temp.cleanup()

    def enable(self):
        os.symlink(os.path.join(self.available, "snes.so"),
                   os.path.join(self.enabled, "snes.so"))

    def host(self, commands, env=None):
        args = [HOST, "--protocol", "ndjson-v1", "--interactive",
                "--available-dir", self.available, "--enabled-dir", self.enabled]
        p = subprocess.Popen(args, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        out, err = p.communicate("".join(json.dumps(c) + "\n" for c in commands).encode("utf-8"))
        return p.returncode, [json.loads(s) for s in out.decode("utf-8").splitlines()], err.decode("utf-8")

    def test_actual_upstream_lo_rom_matches_existing_acceptance_runner(self):
        self.enable()
        host_file = os.path.join(self.temp.name, "host.sfc")
        reference_file = os.path.join(self.temp.name, "reference.sfc")
        rc, events, errors = self.host([
            {"command": "list_cores", "request_id": "catalog"},
            {"command": "get_capabilities", "core": "snes", "request_id": "cap"},
            {"command": "probe", "core": "snes", "request_id": "probe"},
            {"command": "read_rom", "core": "snes", "request_id": "read",
             "output": host_file}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertTrue(events[1]["cores"][0]["usable"])
        self.assertEqual(events[1]["cores"][0]["status"], "ready")
        self.assertEqual(events[2]["selection_schema"]["schema_id"],
                         "snes-standard-v1")
        self.assertEqual(events[2]["slot"], "snes")
        self.assertEqual(events[2]["selection_schema"]["selection_modes"],
                         ["auto", "layout", "override"])
        self.assertEqual([field["id"] for field in
                          events[2]["selection_schema"]["fields"]],
                         ["mapping", "rom_size_bytes"])
        self.assertEqual(events[3]["event"], "probe_result")
        self.assertEqual(events[3]["selection_schema_id"], "snes-standard-v1")
        self.assertEqual(events[3]["identification_status"], "unknown")
        self.assertFalse(events[3]["selection_required"])
        self.assertEqual(events[3]["candidates"], [])
        self.assertEqual(events[3]["read_layouts"][0]["id"], "snes-lo")
        self.assertEqual(events[3]["mapping"], "lo")
        self.assertEqual(events[3]["size_bytes"], 524288)
        self.assertEqual(events[-1]["event"], "result")
        self.assertEqual(events[-1]["bytes_written"], 524288)
        self.assertEqual(events[-1]["read_configuration"]["mapping"], "lo")
        self.assertEqual(events[-1]["verification"]["status"], "not_checked")
        self.assertFalse(events[-1]["verified"])
        self.assertEqual([x["percent"] for x in events if x["event"] == "progress"][-1], 100)
        r = subprocess.Popen([ORIGINAL_RUNNER, "--action", "read-rom",
                              "--output", reference_file],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        _, err = r.communicate()
        self.assertEqual(r.returncode, 0, err)
        with io.open(host_file, "rb") as f:
            data = f.read()
        with io.open(reference_file, "rb") as f:
            reference = f.read()
        self.assertEqual(data, reference)
        self.assertEqual(events[-1]["crc32"],
                         "%08X" % (zlib.crc32(data) & 0xffffffff))


    def test_v3_auto_hirom_and_explicit_layout(self):
        self.enable()
        env = os.environ.copy()
        env["RETRODE_MOCK_MAP"] = "hi"
        for mode, chosen in (("auto", None),
                             ("layout", {"version": 1, "mode": "layout",
                                         "schema_id": "snes-standard-v1",
                                         "layout_id": "snes-hi"})):
            target = os.path.join(self.temp.name, "v3-hi-" + mode + ".sfc")
            cmd = {"command": "read_rom", "core": "snes",
                   "request_id": mode, "output": target}
            if chosen:
                cmd["selection"] = chosen
            rc, events, errors = self.host([
                {"command": "probe", "core": "snes", "request_id": "p"}, cmd
            ], env=env)
            self.assertEqual(rc, 0, errors)
            self.assertEqual(events[1]["read_layouts"][0]["id"], "snes-hi")
            self.assertEqual(events[-1]["event"], "result", events)
            self.assertEqual(events[-1]["bytes_written"], 524288)
            self.assertEqual(events[-1]["read_configuration"]["mapping"], "hi")
            self.assertFalse(events[-1]["verification"]["verified"])
            with io.open(target, "rb") as stream:
                payload = stream.read()
            self.assertEqual(len(payload), 524288)
            self.assertEqual(events[-1]["crc32"],
                             "%08X" % (zlib.crc32(payload) & 0xffffffff))
            # First standard HiROM bank is $C0 and starts at address $0000.
            self.assertEqual(payload[0], (0xc0 * 13) & 255)

    def test_v3_manual_mapping_correction_and_smaller_rom(self):
        self.enable()
        target = os.path.join(self.temp.name, "v3-manual-hi.sfc")
        cmd = {"command": "read_rom", "core": "snes", "request_id": "override",
               "output": target,
               "selection": {"version": 1, "mode": "override",
                             "schema_id": "snes-standard-v1",
                             "parameters": {"mapping": "hi",
                                            "rom_size_bytes": 262144}}}
        rc, events, errors = self.host([cmd])
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[-1]["event"], "result", events)
        self.assertEqual(events[-1]["bytes_written"], 262144)
        self.assertEqual(events[-1]["selection_mode"], "override")
        self.assertEqual(events[-1]["read_configuration"]["mapping"], "hi")
        self.assertEqual(events[-1]["read_configuration"]["rom_size_bytes"], 262144)
        self.assertEqual(events[-1]["verification"]["status"], "not_checked")
        with io.open(target, "rb") as stream:
            payload = stream.read()
        self.assertEqual(len(payload), 262144)
        self.assertEqual(payload[0], (0xc0 * 13) & 255)

    def test_v3_rejects_unsafe_or_malformed_requests(self):
        self.enable()
        def choice(mode, **kwargs):
            result = {"version": 1, "mode": mode, "schema_id": "snes-standard-v1"}
            result.update(kwargs)
            return result
        requests = [
            choice("candidate", candidate_id="made-up-title"),
            choice("layout", layout_id="snes-hi"),
            choice("layout", layout_id="snes-lo", parameters={"mapping": "hi"}),
            choice("auto", layout_id="snes-lo"),
            choice("auto", parameters={"mapping": "hi"}),
            choice("override", parameters={"mapping": "exhi",
                                            "rom_size_bytes": 262144}),
            choice("override", parameters={"mapping": "lo",
                                            "rom_size_bytes": 65536}),
            choice("override", parameters={"mapping": "lo",
                                            "rom_size_bytes": 4194304}),
            choice("override", parameters={"mapping": "hi",
                                            "rom_size_bytes": 131072 + 32768}),
            choice("override", parameters={"mapping": "lo",
                                            "rom_size_bytes": "262144"}),
            choice("override", parameters={"mapping": "lo"}),
            choice("override", parameters={"mapping": "lo",
                                            "rom_size_bytes": 262144,
                                            "voltage": 5}),
            choice("override", parameters={"mapping": "lo",
                                            "rom_size_bytes": 262144,
                                            "sram": True}),
            {"version": 2, "mode": "auto", "schema_id": "snes-standard-v1"},
            {"version": 1, "mode": "auto", "schema_id": "snes-standard-v0"}
        ]
        for index, selection in enumerate(requests):
            target = os.path.join(self.temp.name, "snes-bad-%d.sfc" % index)
            rc, events, errors = self.host([{
                "command": "read_rom", "core": "snes",
                "request_id": str(index), "output": target,
                "selection": selection
            }])
            self.assertEqual(rc, 0, errors)
            self.assertEqual(events[-1]["event"], "error", (selection, events))
            self.assertFalse(os.path.exists(target))
            self.assertFalse(any(".partial-" in name
                                 for name in os.listdir(self.temp.name)))

    def test_v3_special_chip_blocked_even_with_override(self):
        self.enable()
        env = os.environ.copy()
        env["RETRODE_MOCK_SNES_SPECIAL"] = "1"
        target = os.path.join(self.temp.name, "snes-special.sfc")
        rc, events, errors = self.host([{
            "command": "read_rom", "core": "snes", "request_id": "blocked",
            "output": target,
            "selection": {"version": 1, "mode": "override",
                          "schema_id": "snes-standard-v1",
                          "parameters": {"mapping": "lo",
                                         "rom_size_bytes": 262144}}
        }], env=env)
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[-1]["event"], "error")
        self.assertFalse(os.path.exists(target))

    def test_original_snes_core_one_shot_probe(self):
        self.enable()
        args = [HOST, "--protocol", "ndjson-v1", "--core", "snes",
                "--action", "probe",
                "--available-dir", self.available, "--enabled-dir", self.enabled]
        p = subprocess.Popen(args, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        out, err = p.communicate()
        self.assertEqual(p.returncode, 0, err)
        event = json.loads(out.decode("utf-8"))
        self.assertEqual(event["event"], "probe_result")
        self.assertEqual(event["size_bytes"], 524288)
        self.assertEqual(event["mapping"], "lo")
        self.assertEqual(event["checksum"], "1234")

    def test_disabled_real_module_is_visible_but_not_usable(self):
        rc, events, errors = self.host([
            {"command": "list_cores", "request_id": "catalog"},
            {"command": "probe", "core": "snes", "request_id": "probe"}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[1]["cores"][0]["status"], "disabled")
        self.assertFalse(events[1]["cores"][0]["usable"])
        self.assertEqual(events[2]["event"], "error")

    def test_real_core_cancel_and_temporary_output_cleanup(self):
        self.enable()
        file_path = os.path.join(self.temp.name, "cancel.sfc")
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "snes", "request_id": "job",
             "output": file_path},
            {"command": "cancel", "request_id": "job"}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertTrue(any(e.get("code") == "cancelled" for e in events))
        self.assertFalse(os.path.exists(file_path))
        self.assertEqual([name for name in os.listdir(self.temp.name)
                          if name.startswith("cancel.sfc.partial-")], [])


if __name__ == "__main__":
    unittest.main()
