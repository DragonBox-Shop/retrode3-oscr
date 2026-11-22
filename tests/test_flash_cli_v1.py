"""Offline Flash ABI/CLI tests. No real kernel, bus writes or chip probing."""
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "build/migration/retrode3-cli-mock")
CORE = os.path.join(ROOT, "build/migration/registry-fixtures/flashmock.so")


class FlashCliContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.temp.name, "available")
        self.enabled = os.path.join(self.temp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        dest = os.path.join(self.available, "flashmock.so")
        shutil.copyfile(CORE, dest)
        with open(os.path.join(self.available, "flashmock.core"), "w") as f:
            f.write("id=flashmock\nname=Mock Flash\nversion=0.1.0-test\nslot=md\n")
        os.symlink(dest, os.path.join(self.enabled, "flashmock.so"))

    def tearDown(self):
        self.temp.cleanup()

    def cli(self, commands):
        proc = subprocess.Popen(
            [HOST, "--protocol", "ndjson-v1", "--interactive",
             "--available-dir", self.available, "--enabled-dir", self.enabled],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        data = "".join(json.dumps(c) + "\n" for c in commands).encode("utf-8")
        out, err = proc.communicate(data)
        self.assertEqual(proc.returncode, 0, err.decode("utf-8"))
        return [json.loads(line) for line in out.decode("utf-8").splitlines()]

    def test_flash_metadata_has_no_bus_probe(self):
        events = self.cli([
            {"command": "get_flash_capabilities", "core": "flashmock", "request_id": "meta"}])
        result = events[-1]
        self.assertEqual(result["event"], "flash_capabilities")
        self.assertEqual(result["request_id"], "meta")
        self.assertEqual(result["slot"], "md")
        self.assertTrue(result["available"])
        self.assertFalse(result["write_supported"])
        self.assertFalse(result["capabilities"]["write_supported"])

    def test_explicit_probe_reports_geometry_without_write_permission(self):
        events = self.cli([
            {"command": "flash_probe", "core": "flashmock", "request_id": "probe"}])
        result = events[-1]
        self.assertEqual(result["event"], "flash_probe_result")
        self.assertTrue(result["identified"])
        self.assertFalse(result["write_supported"])
        self.assertEqual(result["capacity_bytes"], 4194304)
        self.assertEqual(result["regions"],
                         [{"start_bytes": 0, "block_bytes": 65536, "blocks": 64}])
        self.assertEqual(result["adapter_profile"], "offline-simulated")

    def test_no_implicit_flash_on_normal_probe(self):
        events = self.cli([
            {"command": "probe", "core": "flashmock", "request_id": "rom"},
            {"command": "get_flash_capabilities", "core": "flashmock", "request_id": "meta"}])
        self.assertIn(events[1]["event"], ("probe_result", "probe"))
        self.assertEqual(events[-1]["event"], "flash_capabilities")

    def test_write_commands_are_blocked_before_kernel_and_ui_approval(self):
        events = self.cli([
            {"command": "flash_write", "core": "flashmock", "request_id": "deny"},
            {"command": "flash_probe", "core": "flashmock",
             "request_id": "bad", "output": "/tmp/do-not-write"},
            {"command": "flash_probe", "core": "flashmock",
             "request_id": "ok"}])
        self.assertEqual([e["event"] for e in events[1:]],
                         ["error", "error", "flash_probe_result"])
        self.assertFalse(events[-1]["write_supported"])


if __name__ == "__main__":
    unittest.main()
