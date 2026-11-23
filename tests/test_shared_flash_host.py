"""Hardware-free Flash actions through the real shared CLI (Python 3.4 compatible)."""
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "build/migration/retrode3-cli-mock")
FIXTURE = os.path.join(ROOT, "build/migration/mock-flash-core.so")


class FlashJobContract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.available = os.path.join(self.tmp.name, "available")
        self.enabled = os.path.join(self.tmp.name, "enabled")
        os.mkdir(self.available)
        os.mkdir(self.enabled)
        dst = os.path.join(self.available, "flash-mock.so")
        shutil.copyfile(FIXTURE, dst)
        with open(os.path.join(self.available, "flash-mock.core"), "w") as out:
            out.write("id=flash-mock\nname=Flash fixture\nversion=1-test\nslot=md\n")
        os.symlink(dst, os.path.join(self.enabled, "flash-mock.so"))
        self.source = os.path.join(self.tmp.name, "game.md")
        with open(self.source, "wb") as out:
            out.write(bytes(bytearray((i * 31) % 256 for i in range(1024))))
        self.blank = os.path.join(self.tmp.name, "blank.md")
        with open(self.blank, "wb") as out:
            out.write(b"\xff" * 512)

    def tearDown(self):
        self.tmp.cleanup()

    def call(self, commands, env_updates=None):
        env = dict(os.environ)
        if env_updates:
            env.update(env_updates)
        argv = [HOST, "--protocol", "ndjson-v1", "--interactive",
                "--available-dir", self.available, "--enabled-dir", self.enabled]
        p = subprocess.Popen(argv, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        out, err = p.communicate(("".join(json.dumps(c) + "\n" for c in commands)).encode("utf-8"))
        self.assertEqual(p.returncode, 0, err.decode("utf-8", "replace"))
        return [json.loads(x) for x in out.decode("utf-8").splitlines()]

    def cmd(self, action, request="job", source=None):
        x = {"command": action, "core": "flash-mock", "request_id": request}
        if source is not None:
            x["source"] = source
        return x

    def test_dynamic_actions_and_physical_id(self):
        events = self.call([
            {"command": "list_cores", "request_id": "list"},
            self.cmd("get_capabilities", "caps"),
            self.cmd("probe_flash", "probe")])
        actions = events[1]["cores"][0]["actions"]
        for action in ("probe_flash", "write_flash", "verify_flash", "flash_operation"):
            self.assertIn(action, actions)
        self.assertTrue(events[2]["write_operations"])
        self.assertEqual(events[3]["event"], "flash_probe_result")
        self.assertEqual(events[3]["capacity_bytes"], 4194304)
        self.assertEqual(events[3]["sector_count"], 71)

    def test_write_has_phases_and_only_completes_after_verify(self):
        events = self.call([self.cmd("write_flash", source=self.source)])
        states = [x for x in events if x["event"] == "progress"]
        phases = [x["phase"] for x in states]
        for name in ("erase", "program", "verify"):
            self.assertIn(name, phases)
            numbers = [x["percent"] for x in states if x["phase"] == name]
            self.assertEqual(numbers[0], 0)
            self.assertEqual(numbers[-1], 100)
            self.assertEqual(numbers, sorted(numbers))
        self.assertEqual(events[-1]["event"], "result")
        self.assertTrue(events[-1]["flash_verification"]["verified"])
        self.assertEqual(events[-1]["bytes_compared"], 1024)

    def test_mismatch_never_succeeds_and_reports_offset(self):
        events = self.call([self.cmd("write_flash", source=self.source)],
                           {"RETRODE3_TEST_FLASH_MISMATCH": "1"})
        self.assertEqual(events[-1]["event"], "error")
        self.assertIn("offset 7", events[-1]["message"])
        self.assertFalse(any(x["event"] == "result" for x in events))

    def test_erase_failure_never_reports_success(self):
        events = self.call([self.cmd("write_flash", source=self.source)],
                           {"RETRODE3_TEST_FLASH_FAIL_ERASE": "1"})
        self.assertEqual(events[-1]["event"], "error")
        self.assertFalse(any(e["event"] == "result" for e in events))

    def test_standalone_verify_cannot_write(self):
        events = self.call([self.cmd("verify_flash", source=self.blank)])
        phases = [x["phase"] for x in events if x["event"] == "progress"]
        self.assertTrue(phases)
        self.assertEqual(set(phases), {"verify"})
        self.assertEqual(events[-1]["event"], "result")

    def test_unsupported_chip_fails_before_destructive_work(self):
        events = self.call([self.cmd("write_flash", source=self.source)],
                           {"RETRODE3_TEST_FLASH_BAD_ID": "1"})
        self.assertEqual(events[-1]["event"], "error")
        self.assertFalse(any(e["event"] == "progress" for e in events))


    def stage(self, erase, write, verify, passes, source=None, request="stages"):
        job = {"command": "flash_operation", "core": "flash-mock",
               "request_id": request, "erase": erase, "write": write,
               "verify": verify, "verify_passes": passes}
        if source:
            job["source"] = source
        return job

    def test_independent_erase_without_rom_erases_entire_chip(self):
        events = self.call([self.stage(True, False, False, 0)])
        result = events[-1]
        self.assertEqual(result["event"], "result")
        self.assertEqual(result["sectors_erased"], 71)
        self.assertEqual(result["bytes_programmed"], 0)
        self.assertEqual(result["flash_verification"]["status"], "not_checked")
        self.assertFalse(result["flash_verification"]["verified"])
        self.assertEqual(set(x["phase"] for x in events
                             if x["event"] == "progress"), {"erase"})

    def test_erase_write_without_verify(self):
        events = self.call([self.stage(True, True, False, 0, self.source)])
        result = events[-1]
        self.assertEqual(result["event"], "result")
        self.assertEqual(result["bytes_programmed"], 1024)
        self.assertEqual(result["sectors_erased"], 1)
        self.assertEqual(result["flash_verification"]["status"], "not_checked")
        self.assertEqual(set(x["phase"] for x in events
                             if x["event"] == "progress"), {"erase", "program"})

    def test_verify_only_with_multiple_passes_and_no_programming(self):
        events = self.call([self.stage(False, False, True, 4, self.blank)])
        result = events[-1]
        self.assertEqual(result["event"], "result")
        self.assertEqual(result["flash_verification"]["passes_completed"], 4)
        self.assertEqual(result["flash_verification"]["status"], "verified")
        self.assertEqual(result["bytes_compared"], 2048)
        states = [x for x in events if x["event"] == "progress"]
        self.assertEqual(set(x["phase"] for x in states), {"verify"})
        self.assertEqual(states[-1]["verify_pass"], 4)
        self.assertEqual(states[-1]["verify_passes"], 4)
        self.assertEqual(states[-1]["percent"], 100)

    def test_mismatch_in_repeated_verify_does_not_claim_success(self):
        events = self.call([self.stage(True, True, True, 3, self.source)],
                           {"RETRODE3_TEST_FLASH_MISMATCH": "1"})
        self.assertEqual(events[-1]["event"], "error")
        self.assertIn("pass 1/3", events[-1]["message"])
        self.assertFalse(any(x["event"] == "result" for x in events))

    def test_invalid_stage_combinations_rejected(self):
        invalid = [
            self.stage(False, False, False, 0),
            self.stage(False, True, False, 0),
            self.stage(True, False, True, 0, self.blank),
            self.stage(True, False, False, 1),
            self.stage(True, True, True, 100, self.source),
            self.stage(True, True, True, -1, self.source),
        ]
        for i, item in enumerate(invalid):
            item["request_id"] = "bad" + str(i)
        events = self.call(invalid)
        self.assertEqual(len([x for x in events if x["event"] == "error"]),
                         len(invalid))
        self.assertFalse(any(x["event"] == "result" for x in events))


if __name__ == "__main__":
    unittest.main()
