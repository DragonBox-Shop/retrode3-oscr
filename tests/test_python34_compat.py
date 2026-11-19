"""Prevent pathlib APIs added after Python 3.4 from entering hardware tests.

The real Retrode3 2.9.5 runs Python 3.4 even though GitHub CI uses a newer
interpreter. This static guard tests for the APIs that already broke both
native MegaDrive and SNES acceptance suites on the target device.
"""
import ast
import io
import os
import unittest


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = ("test_native_snes.py", "test_native_md.py",
           "test_real_snes_adapter.py", "test_real_md_adapter.py",
           "test_shared_core_host.py", "test_core_registry.py")
UNSUPPORTED_PATH_METHODS = ("read_bytes", "write_bytes",
                            "read_text", "write_text")


class TestPython34Compat(unittest.TestCase):
    def test_no_python35_path_file_methods_in_target_hardware_tests(self):
        for filename in TARGETS:
            path = os.path.join(ROOT, "tests", filename)
            with io.open(path, "r", encoding="utf-8") as handle:
                tree = ast.parse(handle.read(), filename=path)
            unsupported = [(node.attr, node.lineno)
                           for node in ast.walk(tree)
                           if isinstance(node, ast.Attribute)
                           and node.attr in UNSUPPORTED_PATH_METHODS]
            self.assertEqual(
                unsupported, [],
                "{} uses pathlib methods unavailable on Python 3.4: {}".format(
                    filename, unsupported))


if __name__ == "__main__":
    unittest.main()
