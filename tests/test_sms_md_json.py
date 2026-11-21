"""Confirm the real SMS .so exports parseable, experimental read-only v3 metadata.

Python 3.4-compatible; no cartridge hardware or local CLI is touched.
"""
import ctypes
import json
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULE = os.environ.get('RETRODE3_SMS_TEST_MODULE',
                        os.path.join(ROOT, 'build', 'sms-md.so'))


class OperationsV3(ctypes.Structure):
    _fields_ = [('abi_version', ctypes.c_uint32),
                ('struct_size', ctypes.c_uint32),
                ('capabilities_json', ctypes.c_void_p),
                ('fast_probe_json', ctypes.c_void_p),
                ('plan_read', ctypes.c_void_p),
                ('read_rom', ctypes.c_void_p)]


class DescriptorV1(ctypes.Structure):
    _fields_ = [('abi_version', ctypes.c_uint32),
                ('struct_size', ctypes.c_uint32),
                ('id', ctypes.c_char_p),
                ('name', ctypes.c_char_p),
                ('version', ctypes.c_char_p),
                ('slot', ctypes.c_char_p)]


class SmsCoreAbi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = ctypes.CDLL(MODULE)
        get_ops = cls.module.retrode3_core_get_operations_v3
        get_ops.restype = ctypes.POINTER(OperationsV3)
        cls.ops = get_ops().contents

    def get_json(self, addr):
        fn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t,
                             ctypes.c_void_p, ctypes.c_size_t)(addr)
        out = ctypes.create_string_buffer(8192)
        err = ctypes.create_string_buffer(256)
        self.assertEqual(fn(out, len(out), err, len(err)), 0, err.value)
        return json.loads(out.value.decode('utf-8'))

    def test_discovery_descriptor_matches_installed_manifest_source(self):
        # CLI discovery requires exact equality of all four strings.
        # This test is metadata-only: no actual cartridge or bus I/O.
        get_desc = self.module.retrode3_core_describe_v1
        get_desc.restype = ctypes.POINTER(DescriptorV1)
        desc = get_desc().contents
        manifest = {}
        path = os.path.join(ROOT, 'packaging', 'cores', 'sms-md.core')
        with open(path, 'r') as handle:
            for line in handle:
                key, val = line.rstrip('\n').split('=', 1)
                manifest[key] = val
        self.assertEqual(set(manifest), set(['id', 'name', 'version', 'slot']))
        self.assertEqual(desc.abi_version, 1)
        self.assertEqual(desc.struct_size, ctypes.sizeof(DescriptorV1))
        for key in ['id', 'name', 'version', 'slot']:
            self.assertEqual(getattr(desc, key).decode('utf-8'), manifest[key])

    def test_abi_version_and_valid_schema(self):
        self.assertEqual(self.ops.abi_version, 3)
        self.assertEqual(self.ops.struct_size, ctypes.sizeof(OperationsV3))
        schema = self.get_json(self.ops.capabilities_json)
        self.assertEqual(schema['schema_id'], 'sms-md-layout-v1')
        self.assertEqual([p['id'] for p in schema['fields']],
                         ['mapping', 'rom_kib'])
        self.assertTrue(schema['rules'][0]['read_supported'])
        self.assertFalse(schema['rules'][1]['read_supported'])
        self.assertTrue(schema['fields'][0]['values'][0]['read_supported'])
        self.assertFalse(schema['fields'][0]['values'][1]['read_supported'])

    def test_experimental_probe_policy_without_physical_io(self):
        # This suite loads the production .so, not a TESTING build.
        # Calling fast_probe_json here can access a real /dev/slot-md
        # when the test happens to run on powered Retrode hardware.
        # Probe JSON is exercised with a synthetic voltage/device by
        # sms_md_live_test.cpp; validate the production policy here.
        schema = self.get_json(self.ops.capabilities_json)
        self.assertEqual(schema['hardware_mode'], 'experimental-read-only')
        self.assertEqual(schema['probe_voltage_mv'], 3300)
        self.assertEqual(schema['read_voltage_mv'], 5000)
        self.assertFalse(schema['write_operations'])


if __name__ == '__main__':
    unittest.main()