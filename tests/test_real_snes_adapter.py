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
        self.assertEqual(events[3]["event"], "probe_result")
        self.assertEqual(events[3]["mapping"], "lo")
        self.assertEqual(events[3]["size_bytes"], 524288)
        self.assertEqual(events[-1]["event"], "result")
        self.assertEqual(events[-1]["bytes_written"], 524288)
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
