"""Hardware-free generic v3 NDJSON integration tests (Python 3.4 compatible)."""
import json
import os
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "build/migration/retrode3-cli-mock")
CORE = os.path.join(ROOT, "build/migration/mock-core-v3.so")


class GenericSelectionContract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.tmp.name, "available")
        self.enabled = os.path.join(self.tmp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        dest = os.path.join(self.available, "mock-v3.so")
        import shutil
        shutil.copyfile(CORE, dest)
        with open(os.path.join(self.available, "mock-v3.core"), "w") as out:
            out.write("id=mock-v3\nname=Generic v3 fixture\nversion=1-test\nslot=snes\n")
        os.symlink(dest, os.path.join(self.enabled, "mock-v3.so"))

    def tearDown(self):
        self.tmp.cleanup()

    def call(self, scenario, cmds):
        env = dict(os.environ)
        env["RETRODE_TEST_V3_SCENARIO"] = scenario
        p = subprocess.Popen([HOST, "--protocol", "ndjson-v1", "--interactive",
                              "--available-dir", self.available,
                              "--enabled-dir", self.enabled],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, env=env)
        out, err = p.communicate(("".join(json.dumps(c) + "\n" for c in cmds)).encode("utf-8"))
        self.assertEqual(p.returncode, 0, err.decode("utf-8", "replace"))
        return [json.loads(row) for row in out.decode("utf-8").splitlines()]

    def read(self, selection=None, name="read"):
        cmd = {"command": "read_rom", "core": "mock-v3",
               "request_id": name,
               "output": os.path.join(self.tmp.name, name + ".bin")}
        if selection is not None:
            cmd["selection"] = selection
        return cmd

    def select(self, mode, **kwargs):
        return dict({"version": 1, "mode": mode, "schema_id": "test-v1"}, **kwargs)

    def test_slot_and_dynamic_capabilities(self):
        events = self.call("unique", [
            {"command": "list_cores", "request_id": "cores"},
            {"command": "get_capabilities", "core": "mock-v3", "request_id": "cap"}])
        self.assertEqual(events[1]["cores"][0]["slot"], "snes")
        schema = events[-1]["selection_schema"]
        self.assertEqual(schema["schema_id"], "test-v1")
        self.assertEqual([x["id"] for x in schema["fields"]],
                         ["bank_count", "safe"])

    def test_title_ambiguity_does_not_block_auto_read(self):
        events = self.call("titles", [
            {"command": "probe", "core": "mock-v3", "request_id": "p"},
            self.read(self.select("auto"))])
        self.assertEqual(events[1]["ambiguity"], "titles_only")
        self.assertFalse(events[1]["selection_required"])
        self.assertEqual(len(events[1]["candidates"]), 2)
        self.assertEqual(events[-1]["event"], "result")
        self.assertEqual(events[-1]["bytes_written"], 1028)
        self.assertFalse(events[-1]["verification"]["verified"])

    def test_layout_ambiguity_requires_choice(self):
        choices = [
            self.read(self.select("auto"), "bad-auto"),
            self.read(self.select("layout", layout_id="y"), "selected-y")]
        events = self.call("layouts", [
            {"command": "probe", "core": "mock-v3", "request_id": "p"}
        ] + choices)
        self.assertEqual(events[1]["ambiguity"], "layouts")
        self.assertTrue(events[1]["selection_required"])
        self.assertTrue(any(x.get("event") == "error" and
                            x.get("request_id") == "bad-auto" for x in events))
        self.assertFalse(os.path.exists(choices[0]["output"]))
        self.assertTrue(os.path.exists(choices[1]["output"]))
        self.assertEqual(events[-1]["event"], "result")

    def test_unknown_override_and_rejected_parameters(self):
        good = self.select("override", parameters={"bank_count": 1, "safe": True})
        bad = [
            self.select("override", parameters={"bank_count": 2, "safe": True}),
            self.select("override", parameters={"bank_count": 1, "safe": False}),
            self.select("override", parameters={"bank_count": 1, "safe": True,
                                                 "voltage": 12}),
            self.select("override", parameters={"bank_count": 1, "safe": "true"}),
            self.select("override", parameters={"bank_count": 1}),
            self.select("candidate", candidate_id="a"),
            self.select("layout", layout_id="bogus"),
            self.select("auto"),
        ]
        commands = [self.read(x, "bad-%s" % i) for i, x in enumerate(bad)]
        commands.append(self.read(good, "good"))
        events = self.call("unknown", commands)
        for cmd in commands[:-1]:
            self.assertTrue(any(x.get("request_id") == cmd["request_id"] and
                                x["event"] == "error" for x in events))
            self.assertFalse(os.path.exists(cmd["output"]))
        self.assertTrue(os.path.exists(commands[-1]["output"]))
        self.assertEqual(events[-1]["verification"]["status"], "not_found")

    def test_unsupported_candidate_and_mismatch(self):
        blocked = self.read(self.select("candidate", candidate_id="b"), "blocked")
        events = self.call("blocked", [
            {"command": "probe", "core": "mock-v3", "request_id": "p"},
            blocked])
        self.assertFalse(events[1]["candidates"][1]["read_supported"])
        self.assertEqual(events[-1]["event"], "error")
        self.assertFalse(os.path.exists(blocked["output"]))
        mismatch = self.call("titles", [
            self.read(self.select("candidate", candidate_id="b"), "mismatch")])
        self.assertEqual(mismatch[-1]["verification"]["status"], "mismatch")
        self.assertFalse(mismatch[-1]["verified"])

    def test_no_implicit_override_or_mixed_legacy_fields(self):
        modes = [
            self.select("auto", parameters={"bank_count": 1}),
            self.select("override", parameters={"bank_count": 1, "safe": True},
                        candidate_id="a"),
            self.select("candidate", candidate_id="a", parameters={"safe": True}),
            self.select("layout", layout_id="x", candidate_id="a"),
            {"version": 2, "schema_id": "test-v1", "mode": "auto"},
            {"version": 1, "schema_id": "test-v0", "mode": "auto"},
        ]
        commands = [self.read(x, "invalid-%s" % i) for i, x in enumerate(modes)]
        mixed = self.read(self.select("auto"), "mixed")
        mixed["size_bytes"] = 131072
        commands.append(mixed)
        events = self.call("titles", commands)
        self.assertEqual(len([x for x in events if x["event"] == "error"]),
                         len(commands))
        for cmd in commands:
            self.assertFalse(os.path.exists(cmd["output"]))

    def test_cancel_releases_partial_file(self):
        cmd = self.read(self.select("auto"), "cancelled")
        events = self.call("unique", [
            cmd, {"command": "cancel", "request_id": "cancelled"}])
        self.assertTrue(any(x.get("code") == "cancelled" for x in events), events)
        self.assertFalse(os.path.exists(cmd["output"]))
        self.assertFalse(any(".partial-" in x for x in os.listdir(self.tmp.name)))


if __name__ == "__main__":
    unittest.main()
