"""Offline installed-core and symlink enablement tests (Python 3.4)."""
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOCK = ROOT / "build/migration/retrode3-cli-mock"
NORMAL = ROOT / "build/migration/retrode3-cli"
CORECTL = ROOT / "build/migration/retrode3-corectl-mock"
NORMALCTL = ROOT / "build/migration/retrode3-corectl"
FIXTURES = ROOT / "build/migration/registry-fixtures"


class TestCoreRegistry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for path in (MOCK, NORMAL, CORECTL, NORMALCTL, FIXTURES / "snes.so",
                     FIXTURES / "md.so", FIXTURES / "bad.so"):
            if not path.is_file():
                raise RuntimeError("Run make -f Makefile.migration test-core-registry")

    def make_dirs(self, root):
        available = os.path.join(root, "cores-available")
        enabled = os.path.join(root, "cores-enabled")
        os.mkdir(available)
        os.mkdir(enabled)
        return available, enabled

    def install(self, available, id, name, slot, from_id=None):
        shutil.copyfile(str(FIXTURES / ((from_id or id) + ".so")),
                        os.path.join(available, id + ".so"))
        manifest = "id={0}\nname={1}\nversion=0.1.0-test\nslot={2}\n".format(
            id, name, slot)
        with io.open(os.path.join(available, id + ".core"), "w",
                     encoding="utf-8") as handle:
            handle.write(manifest)

    def enable(self, available, enabled, id, target=None):
        if target is None:
            target = os.path.join(available, id + ".so")
        os.symlink(target, os.path.join(enabled, id + ".so"))

    def cli(self, available, enabled):
        proc = subprocess.Popen(
            [str(MOCK), "--list-cores", "--protocol", "ndjson-v1",
             "--available-dir", str(available), "--enabled-dir", str(enabled)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        return proc.returncode, out.decode("utf-8"), err.decode("utf-8")

    def corectl(self, action, id, available, enabled):
        proc = subprocess.Popen(
            [str(CORECTL), action, id, "--available-dir", str(available),
             "--enabled-dir", str(enabled)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        return proc.returncode, out.decode("utf-8"), err.decode("utf-8")

    def test_control_tool_offline_toggle_is_idempotent_and_safe(self):
        with tempfile.TemporaryDirectory() as temp:
            available, enabled = self.make_dirs(temp)
            self.install(available, "md", "Mega Drive", "md")
            for action in ("enable", "enable"):
                rc, output, err = self.corectl(action, "md", available, enabled)
                self.assertEqual(rc, 0, err)
                self.assertIn("enabled", output)
            self.assertEqual(os.readlink(os.path.join(enabled, "md.so")),
                             os.path.join(available, "md.so"))
            code, stdout, _ = self.cli(available, enabled)
            self.assertEqual(code, 0)
            self.assertTrue(json.loads(stdout)["cores"][0]["enabled"])
            for action in ("disable", "disable"):
                rc, output, err = self.corectl(action, "md", available, enabled)
                self.assertEqual(rc, 0, err)
                self.assertIn("disabled", output)
            self.assertFalse(os.path.lexists(os.path.join(enabled, "md.so")))
            self.assertTrue(os.path.isfile(os.path.join(available, "md.so")))
            self.assertTrue(os.path.isfile(os.path.join(available, "md.core")))

    def test_control_tool_rejects_untrusted_paths_and_ids(self):
        with tempfile.TemporaryDirectory() as temp:
            available, enabled = self.make_dirs(temp)
            self.install(available, "snes", "Super Nintendo", "snes")
            for bad_id in ("../../etc/passwd", "", "UPPER", "a" * 49):
                rc, _, err = self.corectl("enable", bad_id, available, enabled)
                self.assertEqual(rc, 2)
                self.assertIn("invalid core identifier", err)
            rc, _, err = self.corectl("enable", "md", available, enabled)
            self.assertEqual(rc, 2)
            self.assertIn("not installed", err)
            self.enable(available, enabled, "snes",
                        target=os.path.join(temp, "outside.so"))
            for action in ("disable", "enable"):
                rc, _, err = self.corectl(action, "snes", available, enabled)
                self.assertEqual(rc, 2)
                self.assertIn("existing enablement path is invalid", err)
            self.assertTrue(os.path.islink(os.path.join(enabled, "snes.so")))

    def test_production_control_tool_refuses_custom_paths(self):
        proc = subprocess.Popen(
            [str(NORMALCTL), "enable", "md", "--available-dir", "/tmp",
             "--enabled-dir", "/tmp"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 2)
        self.assertEqual(out, b"")
        self.assertIn(b"invalid arguments", err)

    def test_missing_available_dir_has_no_phantom_cores(self):
        with tempfile.TemporaryDirectory() as temp:
            code, stdout, _ = self.cli(
                os.path.join(temp, "not-installed"),
                os.path.join(temp, "not-enabled"))
            self.assertEqual(code, 0)
            event = json.loads(stdout)
            self.assertEqual(event["event"], "cores")
            self.assertEqual(event["cores"], [])

    def test_offline_installed_disabled_then_enabled_then_disabled(self):
        with tempfile.TemporaryDirectory() as temp:
            available, enabled = self.make_dirs(temp)
            self.install(available, "snes", "Super Nintendo", "snes")
            self.install(available, "md", "Mega Drive", "md")
            code, stdout, errors = self.cli(available, enabled)
            self.assertEqual(code, 0, errors)
            result = json.loads(stdout)
            self.assertEqual(result["protocol"], "retrode3-ndjson")
            self.assertEqual(result["version"], 1)
            self.assertEqual(result["request_id"], "cli")
            self.assertEqual([c["id"] for c in result["cores"]], ["md", "snes"])
            self.assertEqual([c["name"] for c in result["cores"]],
                             ["Mega Drive", "Super Nintendo"])
            for core in result["cores"]:
                self.assertTrue(core["installed"])
                self.assertFalse(core["enabled"])
                self.assertFalse(core["usable"])
                self.assertEqual(core["actions"], [])
                self.assertEqual(core["status"], "disabled")

            self.enable(available, enabled, "md")
            self.enable(available, enabled, "snes")
            code, stdout, errors = self.cli(available, enabled)
            self.assertEqual(code, 0, errors)
            for core in json.loads(stdout)["cores"]:
                self.assertTrue(core["enabled"])
                self.assertEqual(core["status"], "discovered")
                self.assertFalse(core["usable"])

            os.unlink(os.path.join(enabled, "md.so"))
            code, stdout, _ = self.cli(available, enabled)
            self.assertEqual(code, 0)
            cores = json.loads(stdout)["cores"]
            self.assertEqual(cores[0]["id"], "md")
            self.assertFalse(cores[0]["enabled"])
            self.assertEqual(cores[0]["status"], "disabled")
            self.assertTrue(cores[1]["enabled"])
            self.assertTrue(os.path.isfile(os.path.join(available, "md.so")))
            self.assertTrue(os.path.isfile(os.path.join(available, "md.core")))

            self.enable(available, enabled, "md")
            code, stdout, _ = self.cli(available, enabled)
            self.assertEqual(code, 0)
            self.assertTrue(json.loads(stdout)["cores"][0]["enabled"])

    def test_incompatible_core_is_not_loaded_when_disabled(self):
        with tempfile.TemporaryDirectory() as temp:
            available, enabled = self.make_dirs(temp)
            self.install(available, "bad", "Super Nintendo", "snes")
            code, stdout, errors = self.cli(available, enabled)
            self.assertEqual(code, 0, errors)
            self.assertEqual(json.loads(stdout)["cores"][0]["status"], "disabled")
            self.assertNotIn("incompatible", errors)
            self.enable(available, enabled, "bad")
            code, stdout, errors = self.cli(available, enabled)
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(stdout)["cores"][0]["status"], "incompatible")
            self.assertTrue(json.loads(stdout)["cores"][0]["enabled"])
            self.assertIn("incompatible", errors)

    def test_metadata_mismatch_in_enabled_module_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            available, enabled = self.make_dirs(temp)
            self.install(available, "wrong", "Mega Drive", "md", from_id="md")
            self.enable(available, enabled, "wrong")
            code, stdout, errors = self.cli(available, enabled)
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(stdout)["cores"][0]["status"], "incompatible")
            self.assertIn("incompatible", errors)

    def test_broken_or_untrusted_enablement_never_loads(self):
        with tempfile.TemporaryDirectory() as temp:
            available, enabled = self.make_dirs(temp)
            self.install(available, "snes", "Super Nintendo", "snes")
            self.enable(available, enabled, "snes",
                        target=os.path.join(temp, "outside.so"))
            code, stdout, errors = self.cli(available, enabled)
            self.assertEqual(code, 0)
            core = json.loads(stdout)["cores"][0]
            self.assertTrue(core["enabled"])
            self.assertEqual(core["status"], "invalid_link")
            self.assertFalse(core["usable"])
            self.assertIn("trusted link", errors)

    def test_missing_module_and_manifest_symlink_are_excluded(self):
        with tempfile.TemporaryDirectory() as temp:
            available, enabled = self.make_dirs(temp)
            self.install(available, "md", "Mega Drive", "md")
            os.remove(os.path.join(available, "md.so"))
            code, stdout, errors = self.cli(available, enabled)
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(stdout)["cores"], [])
            self.assertIn("missing or untrusted", errors)
            shutil.copyfile(str(FIXTURES / "md.so"),
                            os.path.join(available, "md.so"))
            original = os.path.join(available, "md.core")
            os.rename(original, original + ".backup")
            os.symlink(original + ".backup", original)
            code, stdout, errors = self.cli(available, enabled)
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(stdout)["cores"], [])
            self.assertIn("missing or untrusted", errors)

    def test_directory_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            available, enabled = self.make_dirs(temp)
            link = os.path.join(temp, "alias")
            os.symlink(available, link)
            rc, out, err = self.cli(link, enabled)
            self.assertEqual(rc, 2)
            self.assertEqual(out, "")
            self.assertIn("real directory", err)

    def test_production_binary_never_accepts_custom_directories(self):
        with tempfile.TemporaryDirectory() as temp:
            for args in (["--available-dir", temp], ["--enabled-dir", temp]):
                proc = subprocess.Popen([str(NORMAL), "--list-cores"] + args,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = proc.communicate()
                self.assertEqual(proc.returncode, 2)
                self.assertEqual(out, b"")
                self.assertIn(b"usage", err)


if __name__ == "__main__":
    unittest.main()
