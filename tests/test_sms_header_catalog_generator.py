"""Offline SMS product-code candidate catalog regression tests (Python >= 3.4)."""
import io
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, 'tools', 'build-linux-sms-header-catalog.py')
CODES = os.path.join(ROOT, 'third_party', 'oscr-sms-product-codes.json')
NAMES = os.path.join(ROOT, 'third_party', 'oscr-sms-db.json')


class SmsHeaderCatalog(unittest.TestCase):
    def test_action_fighter_revision_ambiguity(self):
        with tempfile.TemporaryDirectory() as root:
            target = os.path.join(root, 'header.h')
            subprocess.check_call([sys.executable, SCRIPT, '--input', CODES,
                                   '--names', NAMES, '--output', target])
            with io.open(target, 'r', encoding='utf-8') as inp:
                output = inp.read()
            self.assertIn('{5055u, 131072u, 0x3658F3E0u,', output)
            self.assertIn('{5055u, 131072u, 0xD91B340Du,', output)
            self.assertIn('Action Fighter (USA, Europe, Brazil)', output)
            self.assertIn('Action Fighter (Japan, Europe)', output)
            self.assertIn('kHeaderCandidateCount', output)

    def test_unreviewed_crosswalk_rejected(self):
        with io.open(CODES, 'r', encoding='utf-8') as inp:
            data = inp.read()
        with tempfile.TemporaryDirectory() as root:
            source = os.path.join(root, 'bad.json')
            output = os.path.join(root, 'header.h')
            with io.open(source, 'w', encoding='utf-8') as dst:
                dst.write(data.replace('5410f81bc8e1900375d076f2f7ee42ee852c2c48',
                                       '0000000000000000000000000000000000000000'))
            proc = subprocess.Popen([sys.executable, SCRIPT, '--input', source,
                                     '--names', NAMES, '--output', output],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            proc.communicate()
            self.assertEqual(proc.returncode, 2)
            self.assertFalse(os.path.exists(output))


if __name__ == '__main__':
    unittest.main()
