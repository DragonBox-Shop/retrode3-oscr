import imp
import struct
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/md-rom-diagnose.py'
mod = imp.load_source('md_rom_diagnose', str(SCRIPT))


def fixture():
    data = bytearray(0x1000)
    data[0x100:0x104] = b'SEGA'
    data[0x120:0x127] = b'SONIC 2'
    data[0x150:0x157] = b'SONIC 2'
    data[0x1a4:0x1a8] = struct.pack('>I', len(data)-1)
    for index in range(0x200,len(data)):
        data[index] = index & 255
    data[0x18e:0x190] = struct.pack('>H',mod.checksum(data))
    return bytes(data)


class DiagnoseTests(unittest.TestCase):
    def test_header_and_internal_checksum(self):
        report = mod.analyze(fixture())
        self.assertTrue(report['sega_header'])
        self.assertTrue(report['size_matches_header'])
        self.assertTrue(report['header_checksum_matches'])
        self.assertEqual(report['overseas_title'],'SONIC 2')

    def test_corrupt_byte_detected(self):
        data = bytearray(fixture())
        data[0x300] ^= 1
        self.assertFalse(mod.analyze(data)['header_checksum_matches'])
        result = mod.compare(fixture(), data)
        self.assertEqual(result['different_bytes'], 1)
        self.assertEqual(result['first_differing_offset'],0x300)
        self.assertEqual(result['even_address_differences'], 1)
        self.assertEqual(result['odd_address_differences'], 0)
        self.assertEqual(result['bit_difference_counts_d0_to_d7'], [1,0,0,0,0,0,0,0])
        self.assertEqual(result['first_20_differences'][0]['hex_offset'], '0x000300')
        self.assertEqual(result['different_bytes_by_64k_block'],
                         [{'block_index': 0, 'difference_count': 1}])

    def test_multiblock_error_distribution(self):
        left = bytearray(131072)
        right = bytearray(left)
        right[1] = 1
        right[65538] = 2
        differences = mod.compare(left, right)
        self.assertEqual(differences['different_bytes'], 2)
        self.assertEqual(differences['different_bytes_by_64k_block'],
                         [{'block_index': 0, 'difference_count': 1},
                          {'block_index': 1, 'difference_count': 1}])

    def test_odd_address_d0_corruption(self):
        reference = bytearray(0x2000)
        corrupted = bytearray(reference)
        corrupted[0x301] = 1
        corrupted[0x503] = 1
        result = mod.compare(corrupted, reference)
        self.assertEqual(result['odd_address_differences'], 2)
        self.assertEqual(result['even_address_differences'], 0)
        self.assertEqual(result['bit_difference_counts_d0_to_d7'], [2,0,0,0,0,0,0,0])
        self.assertEqual(result['bit_transitions_first_one_second_zero'], 2)
        self.assertEqual(result['bit_transitions_first_zero_second_one'], 0)

    def test_compare_command_with_short_second_file(self):
        import tempfile
        import json
        import io
        from contextlib import redirect_stdout
        with tempfile.TemporaryDirectory() as temp:
            first = str(Path(temp) / 'first.bin')
            short = str(Path(temp) / 'short.bin')
            with open(first, 'wb') as handle:
                handle.write(fixture())
            with open(short, 'wb') as handle:
                handle.write(b'bad')
            output = io.StringIO()
            with redirect_stdout(output):
                code = mod.main([first, '--compare', short])
            self.assertEqual(code, 0)
            report = json.loads(output.getvalue())
            self.assertEqual(report['comparison_size_bytes'], 3)
            self.assertTrue(report['comparison']['size_mismatch'])
            self.assertIn('comparison_warning', report)

    def test_swap_and_same_compare(self):
        data = bytes([1,2,3,4,5])
        self.assertEqual(list(mod.swapped_words(data)),[2,1,4,3,5])
        self.assertTrue(mod.compare(data,data)['same'])

    def test_mirrored_halves(self):
        data = bytearray(1024)
        data[0x100:0x104]=b'SEGA'
        self.assertFalse(mod.analyze(data)['halves_identical'])
        duplicated = b'abcd'*128
        self.assertTrue(mod.analyze(duplicated+duplicated)['halves_identical'])


if __name__ == '__main__':
    unittest.main()
