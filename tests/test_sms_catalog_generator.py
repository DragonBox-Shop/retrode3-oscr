"""Pinned, hardware-free OSCR SMS catalog generator regression tests.

Uses the checked-in OSCR data snapshot; never downloads ROMs or DB updates.
Python 3.4 compatible.
"""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, 'tools', 'build-linux-sms-catalog.py')
SOURCE = os.path.join(ROOT, 'third_party', 'oscr-sms-db.json')


class SmsCatalogGeneration(unittest.TestCase):
    def test_action_fighter_and_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = os.path.join(tmp, 'catalog.h')
            subprocess.check_call([sys.executable, SCRIPT, '--input', SOURCE,
                                   '--output', output])
            with open(output, 'r') as stream:
                header = stream.read()
            self.assertIn('0x3658F3E0u', header)
            self.assertIn('Action Fighter (USA, Europe, Brazil)', header)
            self.assertEqual(header.count('    {0x'), 675)
            self.assertIn('catalog_lookup(std::uint32_t crc)', header)

    def test_modified_database_is_rejected(self):
        with open(SOURCE, 'r') as src:
            data = src.read()
        with tempfile.TemporaryDirectory() as tmp:
            inp = os.path.join(tmp, 'sms.json')
            output = os.path.join(tmp, 'catalog.h')
            with open(inp, 'w') as stream:
                stream.write(data + '\n')
            p = subprocess.Popen([sys.executable, SCRIPT, '--input', inp,
                                  '--output', output],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            p.communicate()
            self.assertEqual(p.returncode, 2)
            self.assertFalse(os.path.exists(output))


if __name__ == '__main__':
    unittest.main()
