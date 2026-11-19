"""Real pinned OSCR MegaDrive C++ core through the shared host (Python 3.4)."""
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
LEGACY = os.path.join(ROOT, "build/migration/oscr-md-native-mock")
MD_MODULE = os.path.join(ROOT, "build/migration/real-md-fixture/md.so")
MANIFEST = os.path.join(ROOT, "packaging/cores/md.core")


class TestRealMdAdapter(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.temp.name, "available")
        self.enabled = os.path.join(self.temp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        shutil.copyfile(MD_MODULE, os.path.join(self.available, "md.so"))
        shutil.copyfile(MANIFEST, os.path.join(self.available, "md.core"))

    def tearDown(self):
        self.temp.cleanup()

    def enable(self):
        os.symlink(os.path.join(self.available, "md.so"),
                   os.path.join(self.enabled, "md.so"))

    def host(self, commands, env=None):
        args = [HOST, "--protocol", "ndjson-v1", "--interactive",
                "--available-dir", self.available, "--enabled-dir", self.enabled]
        proc = subprocess.Popen(args, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                env=env)
        data = "".join(json.dumps(item) + "\n" for item in commands)
        stdout, stderr = proc.communicate(data.encode("utf-8"))
        return (proc.returncode,
                [json.loads(line) for line in stdout.decode("utf-8").splitlines()],
                stderr.decode("utf-8"))

    def test_original_oscr_md_module_matches_legacy_reader_byte_for_byte(self):
        self.enable()
        output = os.path.join(self.temp.name, "shared.md")
        legacy_output = os.path.join(self.temp.name, "legacy.md")
        rc, events, errors = self.host([
            {"command": "list_cores", "request_id": "list"},
            {"command": "get_capabilities", "core": "md", "request_id": "cap"},
            {"command": "probe", "core": "md", "request_id": "probe"},
            {"command": "read_rom", "core": "md", "request_id": "read",
             "output": output}
        ])
        self.assertEqual(rc, 0, errors)
        cores = events[1]["cores"]
        self.assertEqual(len(cores), 1)
        self.assertEqual(cores[0]["id"], "md")
        self.assertTrue(cores[0]["enabled"])
        self.assertTrue(cores[0]["usable"])
        self.assertEqual(cores[0]["status"], "ready")
        self.assertEqual(cores[0]["actions"], ["probe", "read_rom"])
        self.assertEqual(events[2]["event"], "capabilities")
        self.assertFalse(events[2]["write_operations"])
        self.assertEqual(events[3]["event"], "probe_result")
        self.assertEqual(events[3]["mapping"], "linear")
        self.assertEqual(events[3]["size_bytes"], 524288)
        self.assertEqual(events[3]["title"], "MOCK MEGADRIVE CART")
        self.assertEqual(events[3]["checksum"], "1234")
        progress = [x for x in events if x["event"] == "progress"]
        self.assertGreater(len(progress), 2)
        self.assertEqual(progress[0]["bytes_done"], 0)
        self.assertEqual(progress[-1]["bytes_done"], 524288)
        self.assertEqual(progress[-1]["percent"], 100)
        self.assertTrue(all(a["bytes_done"] <= b["bytes_done"]
                            for a, b in zip(progress, progress[1:])))
        result = events[-1]
        self.assertEqual(result["event"], "result")
        self.assertEqual(result["bytes_written"], 524288)
        self.assertIsNone(result["verified"])

        proc = subprocess.Popen([LEGACY, "--action", "read-rom",
                                 "--output", legacy_output],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        _, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, err)
        with io.open(output, "rb") as handle:
            actual = handle.read()
        with io.open(legacy_output, "rb") as handle:
            previous = handle.read()
        self.assertEqual(actual, previous)
        self.assertEqual(actual[0x100:0x104], b"SEGA")
        self.assertEqual(result["crc32"], "%08X" % (zlib.crc32(actual) & 0xffffffff))

    def test_real_md_and_snes_share_one_host_without_protocol_forks(self):
        self.enable()
        # A generic mock SNES module is enough to verify the same CLI can
        # dispatch two independently enabled cores without per-core NDJSON.
        source = os.path.join(ROOT, "build/migration/registry-fixtures/snes.so")
        shutil.copyfile(source, os.path.join(self.available, "snes.so"))
        with io.open(os.path.join(self.available, "snes.core"), "w",
                     encoding="utf-8") as handle:
            handle.write("id=snes\nname=Super Nintendo\n"
                         "version=0.1.0-test\nslot=snes\n")
        os.symlink(os.path.join(self.available, "snes.so"),
                   os.path.join(self.enabled, "snes.so"))
        md_output = os.path.join(self.temp.name, "both-md.bin")
        snes_output = os.path.join(self.temp.name, "both-snes.bin")
        rc, events, errors = self.host([
            {"command": "list_cores", "request_id": "list"},
            {"command": "probe", "core": "md", "request_id": "md-probe"},
            {"command": "probe", "core": "snes", "request_id": "snes-probe"},
            {"command": "read_rom", "core": "md", "request_id": "md-read",
             "output": md_output}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertEqual([x["id"] for x in events[1]["cores"]],
                         ["md", "snes"])
        self.assertTrue(all(x["usable"] for x in events[1]["cores"]))
        probes = [x for x in events if x["event"] == "probe_result"]
        self.assertEqual([(x["system"], x["size_bytes"]) for x in probes],
                         [("md", 524288), ("snes", 131072)])
        results = [x for x in events if x["event"] == "result"]
        self.assertEqual([(x["request_id"], x["bytes_written"]) for x in results],
                         [("md-read", 524288)])
        self.assertEqual(os.stat(md_output).st_size, 524288)

        # Interactive jobs are serialized. A second read command pipelined
        # while the first is running is correctly rejected as "busy" by the
        # host's cancellation polling. Send the second job only after the
        # previous read has completed (a fresh session is sufficient here).
        rc, snes_events, errors = self.host([
            {"command": "read_rom", "core": "snes", "request_id": "snes-read",
             "output": snes_output}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertEqual(snes_events[-1]["event"], "result")
        self.assertEqual(snes_events[-1]["request_id"], "snes-read")
        self.assertEqual(snes_events[-1]["bytes_written"], 131072)
        self.assertEqual(os.stat(snes_output).st_size, 131072)

    def test_md_one_shot_probe_and_host_crc(self):
        self.enable()
        args = [HOST, "--protocol", "ndjson-v1", "--core", "md",
                "--action", "probe",
                "--available-dir", self.available, "--enabled-dir", self.enabled]
        process = subprocess.Popen(args, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        output, errors = process.communicate()
        self.assertEqual(process.returncode, 0, errors)
        response = json.loads(output.decode("utf-8"))
        self.assertEqual(response["event"], "probe_result")
        self.assertEqual(response["system"], "md")
        self.assertEqual(response["mapping"], "linear")
        self.assertEqual(response["size_bytes"], 524288)
        self.assertEqual(response["checksum"], "1234")

    def test_unenabled_md_core_cannot_access_hardware(self):
        rc, events, errors = self.host([
            {"command": "list_cores", "request_id": "list"},
            {"command": "probe", "core": "md", "request_id": "probe"}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[1]["cores"][0]["status"], "disabled")
        self.assertFalse(events[1]["cores"][0]["usable"])
        self.assertEqual(events[2]["event"], "error")

    def test_original_md_rejects_unsupported_carts_before_streaming(self):
        self.enable()
        for mode in ("header", "32x", "svp", "lockon", "size"):
            output = os.path.join(self.temp.name, "bad-" + mode + ".md")
            env = os.environ.copy()
            env["RETRODE_MOCK_MD_BAD"] = mode
            rc, events, errors = self.host([
                {"command": "read_rom", "core": "md", "request_id": mode,
                 "output": output}
            ], env=env)
            self.assertEqual(rc, 0, (mode, errors))
            self.assertEqual(events[-1]["event"], "error", (mode, events))
            self.assertFalse(os.path.exists(output))
            self.assertFalse(any(name.startswith("bad-" + mode + ".md.partial-")
                                 for name in os.listdir(self.temp.name)))

    def test_cancel_keeps_output_absent(self):
        self.enable()
        output = os.path.join(self.temp.name, "cancel.md")
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "md", "request_id": "job",
             "output": output},
            {"command": "cancel", "request_id": "job"}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertTrue(any(e.get("code") == "cancelled" for e in events))
        self.assertFalse(os.path.exists(output))
        self.assertFalse(any(name.startswith("cancel.md.partial-")
                             for name in os.listdir(self.temp.name)))

    def test_existing_output_cannot_be_overwritten(self):
        self.enable()
        output = os.path.join(self.temp.name, "keep.md")
        with io.open(output, "wb") as handle:
            handle.write(b"KEEP")
        rc, events, errors = self.host([
            {"command": "read_rom", "core": "md", "request_id": "keep",
             "output": output}
        ])
        self.assertEqual(rc, 0, errors)
        self.assertEqual(events[-1]["event"], "error")
        with io.open(output, "rb") as handle:
            self.assertEqual(handle.read(), b"KEEP")


if __name__ == "__main__":
    unittest.main()
