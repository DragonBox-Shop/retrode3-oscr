"""Shared, cartridge-agnostic NDJSON operation-host integration (Python 3.4)."""
import io
import fcntl
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "build/migration/retrode3-cli-mock")
FIX = os.path.join(ROOT, "build/migration/registry-fixtures")


class TestSharedHost(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.temp.name, "available")
        self.enabled = os.path.join(self.temp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        for core, name, slot in (("snes", "Super Nintendo", "snes"),
                                 ("md", "Mega Drive", "md")):
            shutil.copyfile(os.path.join(FIX, core + ".so"),
                            os.path.join(self.available, core + ".so"))
            with io.open(os.path.join(self.available, core + ".core"),
                         "w", encoding="utf-8") as f:
                f.write("id={0}\nname={1}\nversion=0.1.0-test\nslot={2}\n"
                        .format(core, name, slot))

    def tearDown(self):
        self.temp.cleanup()

    def enable(self, core):
        os.symlink(os.path.join(self.available, core + ".so"),
                   os.path.join(self.enabled, core + ".so"))

    def run_commands(self, commands):
        args = [CLI, "--protocol", "ndjson-v1", "--interactive",
                "--available-dir", self.available, "--enabled-dir", self.enabled]
        p = subprocess.Popen(args, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdin_data = "".join(json.dumps(x) + "\n" for x in commands).encode("utf-8")
        out, err = p.communicate(stdin_data)
        events = [json.loads(line) for line in out.decode("utf-8").splitlines()]
        return p.returncode, events, err.decode("utf-8")

    def test_installed_not_enabled_never_becomes_runnable(self):
        rc, events, _ = self.run_commands([
            {"command": "list_cores", "request_id": "a"},
            {"command": "probe", "core": "snes", "request_id": "b"}])
        self.assertEqual(rc, 0)
        self.assertEqual(events[0]["event"], "ready")
        cores = events[1]["cores"]
        self.assertEqual([x["id"] for x in cores], ["md", "snes"])
        self.assertFalse(cores[1]["enabled"])
        self.assertFalse(cores[1]["usable"])
        self.assertEqual(events[2]["code"], "operation_failed")

    def test_capabilities_probe_and_streaming_with_crc(self):
        self.enable("snes")
        self.enable("md")
        target = os.path.join(self.temp.name, "test.sfc")
        rc, events, errors = self.run_commands([
            {"command": "list_cores", "request_id": "list"},
            {"command": "get_capabilities", "core": "snes", "request_id": "cap"},
            {"command": "probe", "core": "snes", "request_id": "probe"},
            {"command": "read_rom", "core": "snes", "request_id": "rom",
             "output": target}])
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[0]["event"], "ready")
        self.assertEqual([c["status"] for c in events[1]["cores"]],
                         ["discovered", "ready"])
        self.assertEqual([c["usable"] for c in events[1]["cores"]],
                         [False, True])
        self.assertEqual(events[2]["event"], "capabilities")
        self.assertIn("read_rom", events[2]["commands"])
        self.assertEqual(events[3]["event"], "probe_result")
        self.assertEqual(events[3]["title"], "MOCK SNES")
        self.assertEqual(events[3]["size_bytes"], 131072)
        progress = [e for e in events if e["event"] == "progress"]
        self.assertTrue(len(progress) >= 2)
        self.assertEqual(progress[0]["bytes_done"], 0)
        self.assertEqual(progress[-1]["bytes_done"], 131072)
        self.assertEqual(progress[-1]["percent"], 100)
        result = events[-1]
        self.assertEqual(result["event"], "result")
        with io.open(target, "rb") as f:
            data = f.read()
        self.assertEqual(len(data), 131072)
        self.assertEqual(data[:512],
                         bytes(bytearray((i * 7) & 255 for i in range(512))))
        self.assertEqual(result["crc32"], "%08X" % (zlib.crc32(data) & 0xffffffff))
        self.assertIsNone(result["verified"])
        self.assertEqual(result["output"], target)

    def test_metadata_only_module_rejects_operations(self):
        self.enable("md")
        rc, events, _ = self.run_commands([
            {"command": "probe", "core": "md", "request_id": "probe"},
            {"command": "get_capabilities", "core": "md", "request_id": "cap"}])
        self.assertEqual(rc, 0)
        self.assertEqual([x["code"] for x in events[1:]],
                         ["operation_failed", "operation_failed"])

    def test_cancel_removes_partial_and_does_not_emit_success(self):
        self.enable("snes")
        target = os.path.join(self.temp.name, "cancel.sfc")
        rc, events, err = self.run_commands([
            {"command": "read_rom", "core": "snes", "request_id": "job",
             "output": target},
            {"command": "cancel", "request_id": "job"}])
        self.assertEqual(rc, 0, err)
        self.assertTrue(any(e.get("code") == "cancelled" for e in events))
        self.assertFalse(any(e.get("event") == "result" for e in events))
        self.assertFalse(os.path.exists(target))
        self.assertEqual([x for x in os.listdir(self.temp.name)
                          if x.startswith("cancel.sfc.partial-")], [])

    def test_global_cartridge_bus_lock_rejects_overlap(self):
        self.enable("snes")
        lock_path = os.path.join(self.enabled, ".bus.lock")
        with io.open(lock_path, "wb") as handle:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            rc, events, errors = self.run_commands([
                {"command": "probe", "core": "snes", "request_id": "busy"}
            ])
            self.assertEqual(rc, 0, errors)
            self.assertEqual(events[1]["event"], "error")
            self.assertIn("cartridge bus busy", events[1]["message"])
            fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
        rc, events, errors = self.run_commands([
            {"command": "probe", "core": "snes", "request_id": "free"}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[1]["event"], "probe_result")

    def test_bus_lock_symlink_is_refused(self):
        self.enable("snes")
        lock_path = os.path.join(self.enabled, ".bus.lock")
        outside = os.path.join(self.temp.name, "outside")
        with io.open(outside, "wb") as handle:
            handle.write(b"LOCK")
        os.symlink(outside, lock_path)
        rc, events, _ = self.run_commands([
            {"command": "probe", "core": "snes", "request_id": "lock"}
        ])
        self.assertEqual(rc, 0)
        self.assertEqual(events[1]["event"], "error")
        self.assertFalse(events[1].get("ok", False))
        with io.open(outside, "rb") as handle:
            self.assertEqual(handle.read(), b"LOCK")

    def test_existing_file_and_invalid_path_never_written(self):
        self.enable("snes")
        target = os.path.join(self.temp.name, "keep.sfc")
        with io.open(target, "wb") as f:
            f.write(b"KEEP")
        rc, events, _ = self.run_commands([
            {"command": "read_rom", "core": "snes", "request_id": "first",
             "output": target},
            {"command": "read_rom", "core": "snes", "request_id": "second",
             "output": "/tmp/../tmp/retrode3-bad.sfc"}])
        self.assertEqual(rc, 0)
        self.assertEqual(events[1]["code"], "operation_failed")
        self.assertEqual(events[2]["code"], "operation_failed")
        with io.open(target, "rb") as f:
            self.assertEqual(f.read(), b"KEEP")

    def test_invalid_command_keeps_host_alive(self):
        self.enable("snes")
        rc, events, err = self.run_commands([
            {"command": "probe", "core": "../../bad", "request_id": "bad"},
            {"command": "probe", "core": "snes", "request_id": "good"}])
        self.assertEqual(rc, 0, err)
        self.assertEqual(events[1]["code"], "invalid_command")
        self.assertEqual(events[2]["event"], "probe_result")


if __name__ == "__main__":
    unittest.main()
