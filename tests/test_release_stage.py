"""Ensure the shared runtime exports only the agreed Debian filesystem paths.

Build target is 'make -f Makefile.migration test-release-stage'.
All tests are host independent and Python 3.4 compatible.
"""
import io
import os
import stat
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(ROOT, "build", "migration", "stage")

CORE_RECIPES = os.path.join(ROOT, "packaging", "cores")
CORE_STAGING = "usr/lib/retrode3/cores-available"


def release_cores():
    # *.core recipes are the release allowlist; lab-only plugins belong elsewhere.
    return sorted(item[:-5] for item in os.listdir(CORE_RECIPES)
                  if item.endswith(".core"))


def expected_files():
    expected = {
        "usr/bin/retrode3-cli": 0o755,
        "usr/sbin/retrode3-corectl": 0o755,
    }
    for core in release_cores():
        for ext in (".so", ".core"):
            expected[CORE_STAGING + "/" + core + ext] = 0o644
    return expected


class StageRuntimeContract(unittest.TestCase):
    def test_only_agreed_binary_and_core_files_staged(self):
        found = set()
        for path, dirs, files in os.walk(STAGE):
            for name in files:
                rel = os.path.relpath(os.path.join(path, name), STAGE)
                found.add(rel.replace(os.sep, "/"))
        self.assertEqual(found, set(expected_files().keys()))

    def test_staged_files_have_secure_permissions_and_contents(self):
        for relative, permission in expected_files().items():
            path = os.path.join(STAGE, *relative.split("/"))
            self.assertTrue(os.path.isfile(path), relative)
            self.assertFalse(os.path.islink(path), relative)
            self.assertGreater(os.stat(path).st_size, 0, relative)
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode),
                             permission, relative)

    def test_core_metadata_matches_plugin_names(self):
        for system in release_cores():
            manifest = os.path.join(STAGE, "usr", "lib", "retrode3",
                                    "cores-available", system + ".core")
            recipe = os.path.join(CORE_RECIPES, system + ".core")
            with io.open(manifest, "r", encoding="utf-8") as handle:
                staged = handle.read()
            with io.open(recipe, "r", encoding="utf-8") as handle:
                tracked = handle.read()
            self.assertEqual(staged, tracked, system)
            values = {}
            for line in tracked.splitlines():
                if "=" in line:
                    key, value = line.split("=", 1)
                    values[key] = value
            self.assertEqual(values.get("id"), system)
            self.assertTrue(values.get("slot"))
            self.assertTrue(values.get("name"))
            self.assertTrue(values.get("version"))

    def test_no_core_enablement_or_legacy_overwrites_in_stage(self):
        for rel in ("etc/retrode3/cores-enabled",
                    "usr/local/bin/oscr", "usr/lib/retrode3/legacy"):
            self.assertFalse(os.path.exists(
                os.path.join(STAGE, *rel.split("/"))), rel)


if __name__ == "__main__":
    unittest.main()
