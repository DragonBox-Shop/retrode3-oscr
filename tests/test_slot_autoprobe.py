"""Hardware-free sequential slot detection tests; Python 3.4-compatible syntax."""
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "build/migration/retrode3-cli-mock")


class SlotProbeTests(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="retrode-slot-test-")
        self.available = os.path.join(self.root, "available")
        self.enabled = os.path.join(self.root, "enabled")
        self.sysfs = os.path.join(self.root, "sysfs")
        self.md = os.path.join(self.sysfs, "slot-md")
        for folder in (self.available, self.enabled, self.sysfs, self.md):
            os.mkdir(folder)
        self.set_hardware("active", "3300mV")
        for core_id, name in (("mock-a", "Adapter A"), ("mock-z", "Adapter Z")):
            dest = os.path.join(self.available, core_id + ".so")
            shutil.copyfile(os.path.join(ROOT, "build/migration",
                                         "mock-slot-" + core_id[-1] + ".so"), dest)
            with open(os.path.join(self.available, core_id + ".core"), "w") as f:
                f.write("id=%s\nname=%s\nversion=1-test\nslot=md\n" %
                        (core_id, name))
            os.symlink(dest, os.path.join(self.enabled, core_id + ".so"))

    def tearDown(self):
        shutil.rmtree(self.root)

    def set_hardware(self, sense, vcc):
        for name, value in (("sense", sense), ("vcc", vcc)):
            with open(os.path.join(self.md, name), "w") as f:
                f.write(value + "\n")

    def invoke(self, match="mock-z", commands=None, without_sense=None):
        env = dict(os.environ)
        env["RETRODE3_TEST_SLOT_ROOT"] = self.sysfs
        env["RETRODE_TEST_V3_SLOT_MATCH"] = match
        if without_sense:
            env["RETRODE_TEST_V3_NO_SENSE"] = without_sense
        if commands is None:
            commands = [{"command": "probe_slot", "slot": "md",
                         "request_id": "s"}]
        cmd = [HOST, "--protocol", "ndjson-v1", "--interactive",
               "--available-dir", self.available,
               "--enabled-dir", self.enabled]
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, env=env)
        stdout, stderr = p.communicate(
            "".join(json.dumps(c) + "\n" for c in commands).encode("ascii"))
        self.assertEqual(p.returncode, 0, stderr.decode("utf-8", "replace"))
        return [json.loads(x) for x in stdout.decode("utf-8").splitlines()]

    def test_first_failed_header_then_second_identifies_system(self):
        result = self.invoke()[-1]
        self.assertEqual(result["event"], "slot_probe_result")
        self.assertEqual(result["detection"], "detected")
        self.assertEqual(result["core"], "mock-z")
        self.assertEqual([a["outcome"] for a in result["attempts"]],
                         ["no_match", "matched"])
        self.assertEqual(result["probe_result"]["system"], "mock-z")
        self.assertEqual(result["voltage_status"], "manual_required")
        self.assertEqual(open(os.path.join(self.md, "vcc")).read(), "3300mV\n")

    def test_first_header_match_short_circuits(self):
        result = self.invoke("mock-a")[-1]
        self.assertEqual([a["core"] for a in result["attempts"]], ["mock-a"])

    def test_no_header_match_requires_manual_choice(self):
        result = self.invoke("missing")[-1]
        self.assertEqual(result["detection"], "manual_required")
        self.assertIsNone(result["core"])
        self.assertEqual(result["candidates"], [])
        self.assertEqual(len(result["attempts"]), 2)

    def test_removed_cartridge_cannot_probe(self):
        self.set_hardware("empty", "3300mV")
        result = self.invoke()[-1]
        self.assertEqual(result["detection"], "empty")
        self.assertEqual(result["attempts"], [])

    def test_opt_in_probe_without_sense(self):
        self.set_hardware("empty", "3300mV")
        result = self.invoke(without_sense="mock-z")[-1]
        self.assertEqual(result["detection"], "detected")
        self.assertEqual(result["core"], "mock-z")
        self.assertEqual([a["outcome"] for a in result["attempts"]],
                         ["failed", "matched"])
        self.set_hardware("empty", "5000mV")
        self.assertEqual(self.invoke(without_sense="mock-z")[-1]["detection"], "empty")

    def test_unsafe_probe_voltage_skips_every_adapter(self):
        self.set_hardware("active", "5000mV")
        result = self.invoke()[-1]
        self.assertEqual(result["detection"], "manual_required")
        self.assertEqual([a["outcome"] for a in result["attempts"]],
                         ["skipped_voltage", "skipped_voltage"])

    def test_disabled_cores_not_probed(self):
        os.unlink(os.path.join(self.enabled, "mock-a.so"))
        result = self.invoke("mock-z")[-1]
        self.assertEqual([a["core"] for a in result["attempts"]], ["mock-z"])

    def test_rejects_missing_or_mixed_core_and_slot(self):
        results = self.invoke(commands=[
            {"command": "probe_slot", "request_id": "bad"},
            {"command": "probe_slot", "request_id": "bad2",
             "slot": "md", "core": "mock-a"},
            {"command": "probe_slot", "request_id": "ok", "slot": "md"}])
        self.assertEqual([r["event"] for r in results],
                         ["ready", "error", "error", "slot_probe_result"])


if __name__ == "__main__":
    unittest.main()
