"""Regression: core tests must not recompile unchanged shared CLI binaries.

Runs 'make -n' against a synthetic source tree with controlled mtimes.
No compiler, cartridge hardware or installed files are touched.
Compatible with Python 3.4 on the target device.
"""
import io
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = (
    "src/retrode3/retrode3_cli.cpp",
    "src/retrode3/ndjson_protocol.cpp",
    "src/retrode3/retrode3_corectl.cpp",
    "include/retrode3/core_descriptor_v1.h",
    "include/retrode3/core_operations_v1.h",
    "include/retrode3/core_operations_v2.h",
    "include/retrode3/core_operations_v3.h",
    "include/retrode3/json_v3.h",
    "include/retrode3/ndjson_protocol.h",
    "tests/mock_core_module.cpp",
)
TARGETS = (
    "build/migration/retrode3-cli",
    "build/migration/retrode3-corectl",
    "build/migration/retrode3-cli-mock",
    "build/migration/retrode3-corectl-mock",
    "build/migration/registry-fixtures/snes.so",
    "build/migration/registry-fixtures/md.so",
    "build/migration/registry-fixtures/bad.so",
)


class CoreRegistryIncrementalBuild(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="retrode3-make-incremental-")
        shutil.copyfile(os.path.join(ROOT, "Makefile.migration"),
                        os.path.join(self.tmp, "Makefile.migration"))
        for item in SOURCES:
            self.create_file(item, 1000000000)
        for item in TARGETS:
            self.create_file(item, 1000000020)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def create_file(self, relative, timestamp):
        path = os.path.join(self.tmp, *relative.split("/"))
        parent = os.path.dirname(path)
        if not os.path.isdir(parent):
            os.makedirs(parent)
        with io.open(path, "wb") as handle:
            handle.write(b"fixture")
        os.utime(path, (timestamp, timestamp))

    def touch(self, relative):
        path = os.path.join(self.tmp, *relative.split("/"))
        os.utime(path, (1000000040, 1000000040))

    def commands(self, target):
        proc = subprocess.Popen(
            ["make", "-n", "-f", "Makefile.migration", target],
            cwd=self.tmp, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = proc.communicate()
        self.assertEqual(proc.returncode, 0, err.decode("utf-8", "replace"))
        return out.decode("utf-8", "replace")

    def test_no_cli_rebuild_after_unchanged_core_test(self):
        commands = self.commands("test-core-registry")
        self.assertNotIn("-o build/migration/retrode3-cli", commands)
        self.assertNotIn("-o build/migration/retrode3-corectl", commands)
        self.assertNotIn("-o build/migration/registry-fixtures/", commands)
        self.assertIn("test_core_registry.py", commands)

    def test_v3_header_rebuilds_cli_and_test_cli_but_not_corectl(self):
        self.touch("include/retrode3/core_operations_v3.h")
        commands = self.commands("test-core-registry")
        self.assertIn("-o build/migration/retrode3-cli\n", commands)
        self.assertIn("-o build/migration/retrode3-cli-mock\n", commands)
        self.assertNotIn("-o build/migration/retrode3-corectl", commands)

    def test_corectl_source_only_rebuilds_corectl_pair(self):
        self.touch("src/retrode3/retrode3_corectl.cpp")
        commands = self.commands("test-core-registry")
        self.assertIn("-o build/migration/retrode3-corectl\n", commands)
        self.assertIn("-o build/migration/retrode3-corectl-mock\n", commands)
        self.assertNotIn("-o build/migration/retrode3-cli", commands)

    def test_mock_fixture_change_does_not_rebuild_shared_cli(self):
        self.touch("tests/mock_core_module.cpp")
        commands = self.commands("test-core-registry")
        self.assertIn("-o build/migration/registry-fixtures/snes.so\n", commands)
        self.assertIn("-o build/migration/registry-fixtures/md.so\n", commands)
        self.assertIn("-o build/migration/registry-fixtures/bad.so\n", commands)
        self.assertNotIn("-o build/migration/retrode3-cli", commands)


if __name__ == "__main__":
    unittest.main()
