"""Installable core discovery tests; compatible with Python 3.4."""
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
FIXTURES = ROOT / "build/migration/registry-fixtures"


class TestCoreRegistry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for path in (MOCK, FIXTURES / "snes.so", FIXTURES / "md.so",
                     FIXTURES / "bad.so"):
            if not path.is_file():
                raise RuntimeError("Run make -f Makefile.migration test-core-registry")

    def cli(self, directory, extra=None):
        args = [str(MOCK), "--list-cores", "--protocol", "ndjson-v1",
                "--core-dir", str(directory)]
        if extra:
            args.extend(extra)
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        return proc.returncode, out.decode("utf-8"), err.decode("utf-8")

    def test_missing_dir_has_no_phantom_cores(self):
        with tempfile.TemporaryDirectory() as temp:
            rc, stdout, _ = self.cli(os.path.join(temp, "not-installed"))
            self.assertEqual(rc, 0)
            event = json.loads(stdout)
            self.assertEqual(event["event"], "cores")
            self.assertEqual(event["cores"], [])

    def test_install_and_uninstall_detected_without_rebuilding_cli(self):
        with tempfile.TemporaryDirectory() as temp:
            shutil.copyfile(str(FIXTURES / "snes.so"), os.path.join(temp, "snes.so"))
            shutil.copyfile(str(FIXTURES / "md.so"), os.path.join(temp, "md.so"))
            rc, out, _ = self.cli(temp)
            self.assertEqual(rc, 0)
            result = json.loads(out)
            self.assertEqual(result["protocol"], "retrode3-ndjson")
            self.assertEqual(result["version"], 1)
            self.assertEqual(result["request_id"], "cli")
            self.assertEqual([c["id"] for c in result["cores"]], ["md", "snes"])
            self.assertEqual([c["slot"] for c in result["cores"]], ["md", "snes"])
            for core in result["cores"]:
                self.assertTrue(core["installed"])
                self.assertFalse(core["usable"])
                self.assertEqual(core["actions"], [])
                self.assertEqual(core["status"], "discovered")
            os.remove(os.path.join(temp, "md.so"))
            rc, out, _ = self.cli(temp)
            self.assertEqual(rc, 0)
            self.assertEqual([c["id"] for c in json.loads(out)["cores"]], ["snes"])
            os.remove(os.path.join(temp, "snes.so"))
            rc, out, _ = self.cli(temp)
            self.assertEqual(rc, 0)
            self.assertEqual(json.loads(out)["cores"], [])

    def test_incompatible_and_bad_name_not_available(self):
        with tempfile.TemporaryDirectory() as temp:
            shutil.copyfile(str(FIXTURES / "bad.so"), os.path.join(temp, "bad.so"))
            shutil.copyfile(str(FIXTURES / "md.so"), os.path.join(temp, "wrong.so"))
            rc, out, errors = self.cli(temp)
            self.assertEqual(rc, 0)
            self.assertEqual(json.loads(out)["cores"], [])
            self.assertIn("incompatible discovery ABI", errors)
            self.assertIn("invalid identity", errors)

    def test_symlinks_are_never_loaded(self):
        with tempfile.TemporaryDirectory() as temp:
            os.symlink(str(FIXTURES / "snes.so"), os.path.join(temp, "snes.so"))
            rc, out, errors = self.cli(temp)
            self.assertEqual(rc, 0)
            self.assertEqual(json.loads(out)["cores"], [])
            self.assertIn("symlinks are forbidden", errors)

    def test_directory_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            inner = os.path.join(temp, "inner")
            os.mkdir(inner)
            link = os.path.join(temp, "alias")
            os.symlink(inner, link)
            rc, out, err = self.cli(link)
            self.assertEqual(rc, 2)
            self.assertEqual(out, "")
            self.assertIn("real directory", err)

    def test_production_binary_never_accepts_custom_core_dir(self):
        if not NORMAL.is_file():
            self.skipTest("Build make -f Makefile.migration native-core-registry first")
        with tempfile.TemporaryDirectory() as temp:
            proc = subprocess.Popen([str(NORMAL), "--list-cores", "--core-dir", temp],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            out, err = proc.communicate()
            self.assertEqual(proc.returncode, 2)
            self.assertEqual(out, b"")
            self.assertIn(b"usage", err)


if __name__ == "__main__":
    unittest.main()
