#!/usr/bin/env python3
"""Pinned-upstream and no-manual-SMS-logic checks. Python 3.4 compatible."""
import hashlib
import importlib.util
import io
import os
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
GENERATE = os.path.join(ROOT, 'tools', 'build-linux-sms-core.py')
_SPEC = importlib.util.spec_from_file_location('sms_oscr_generator', GENERATE)
GEN = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(GEN)
UPSTREAM = os.path.join(ROOT, 'third_party', 'oscr-firmware',
                        'src', 'cores', 'SMS.cpp')


class SmsOriginalGenerationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with io.open(UPSTREAM, 'r', encoding='utf-8', newline='') as handle:
            cls.original = handle.read()

    def test_pinned_original_has_expected_git_blob_sha(self):
        encoded = self.original.encode('utf-8')
        git_blob = b'blob ' + str(len(encoded)).encode('ascii') + b'\0' + encoded
        self.assertEqual(hashlib.sha1(git_blob).hexdigest(),
                         GEN.PINNED_SMS_BLOB)

    def test_codegen_reproducible_and_from_original(self):
        size, read = GEN.extract(self.original)
        with io.open(os.path.join(ROOT, 'build', 'migration',
                     'oscr_sms_size.h'), 'r', encoding='utf-8') as file:
            self.assertEqual(size, file.read())
        with io.open(os.path.join(ROOT, 'build', 'migration',
                     'oscr_sms_read.h'), 'r', encoding='utf-8') as file:
            self.assertEqual(read, file.read())
        # The upstream bank/buffer loops remain, not a second reader.
        self.assertIn('for (uint8_t currBank = 0x0;', read)
        self.assertIn('currBuffer += 512', read)
        self.assertIn('switch (cartNib)', size)
        self.assertNotIn('readByte(', read)
        self.assertNotIn('writeByte(', read)
        self.assertIn('bus.select_page2(currBank)', read)
        self.assertIn('if (banked)', read)
        self.assertIn('bus.read(addr, buffer, sizeof(buffer))', read)

    def test_unreviewed_algorithm_change_fails_closed(self):
        altered = self.original.replace('writeByte(0xFFFF, currBank);',
                                        'writeByte(0xFFFF, currBank + 1);')
        self.assertNotEqual(altered, self.original)
        with self.assertRaises(ValueError):
            GEN.extract(altered)
        # Even changes outside the extracted body require manual review.
        with self.assertRaises(ValueError):
            GEN.extract(self.original + '\n// unexpected new upstream line\n')


if __name__ == '__main__':
    unittest.main()