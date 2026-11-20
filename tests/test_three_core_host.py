"""Load genuine MD/NES/SNES original-derived .so modules in one shared CLI.

Real reader algorithms with mock kernel entrypoints, Python 3.4 compatible.
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


class TripleCoreHost(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.tmp.name, "available")
        self.enabled = os.path.join(self.tmp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        for core, name, version, slot in (
            ("md", "Mega Drive", "0.2.0-v3", "md"),
            ("nes", "Nintendo Entertainment System", "0.2.0-nrom-mmc1", "nes"),
            ("snes", "Super Nintendo", "0.2.0-v3", "snes")
        ):
            source = os.path.join(ROOT, "build/migration/real-" + core + "-fixture", core + ".so")
            shutil.copyfile(source, os.path.join(self.available, core + ".so"))
            with io.open(os.path.join(self.available, core + ".core"), "w",
                         encoding="utf-8") as f:
                f.write("id=" + core + "\nname=" + name +
                        "\nversion=" + version + "\nslot=" + slot + "\n")
            os.symlink(os.path.join(self.available, core + ".so"),
                       os.path.join(self.enabled, core + ".so"))

    def tearDown(self):
        self.tmp.cleanup()

    def invoke(self, core, action, output=None):
        args = [HOST, "--protocol", "ndjson-v1",
                "--available-dir", self.available,
                "--enabled-dir", self.enabled]
        if action == "list":
            args.append("--list-cores")
        else:
            args.extend(["--core", core, "--action", action])
            if output is not None:
                args.extend(["--output", output])
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        events = [json.loads(x) for x in out.decode("utf-8").splitlines()]
        return proc.returncode, events, err.decode("utf-8", "replace")

    def test_one_host_three_real_core_adapters(self):
        rc, events, err = self.invoke(None, "list")
        self.assertEqual(rc, 0, err)
        self.assertEqual([x["id"] for x in events[0]["cores"]], ["md", "nes", "snes"])
        self.assertTrue(all(x["usable"] and x["enabled"] and
                            x["status"] == "ready" for x in events[0]["cores"]))
        for core, size in (("md", 524288), ("nes", 24592), ("snes", 524288)):
            rc, events, err = self.invoke(core, "probe")
            self.assertEqual(rc, 0, (core, events, err))
            self.assertEqual(events[0]["event"], "probe_result")
            self.assertEqual(events[0]["size_bytes"], size)
            target = os.path.join(self.tmp.name, core + "-dump.bin")
            rc, events, err = self.invoke(core, "read-rom", target)
            self.assertEqual(rc, 0, (core, events, err))
            self.assertEqual(events[-1]["event"], "result")
            self.assertEqual(events[-1]["bytes_written"], size)
            self.assertTrue(any(e.get("event") == "progress" for e in events))
            with io.open(target, "rb") as f:
                data = f.read()
            self.assertEqual(len(data), size)
            self.assertEqual(events[-1]["crc32"],
                             "%08X" % (zlib.crc32(data) & 0xffffffff))


if __name__ == "__main__":
    unittest.main()
