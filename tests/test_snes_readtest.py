"""Mock-based tests for standard OSCR SNES header and bank layouts."""
import imp
import io
import json
import tempfile
from contextlib import redirect_stdout
from pathlib import Path
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/retrode3-readtest.py'
reader = imp.load_source('retrode3_snes_test_reader', str(SCRIPT))


def make_rom(mapping='lo', size=524288, chips=0, valid=True):
    rom = bytearray(size)
    span = 32768 if mapping == 'lo' else 65536
    for n in range(size // span):
        rom[n * span:(n + 1) * span] = bytes([n + 1]) * span
    header = bytearray(80)
    header[0x10:0x19] = b'TEST GAME'
    header[0x25] = 0x20 if mapping == 'lo' else 0x21
    header[0x26] = chips
    header[0x27] = size.bit_length() - 11  # log2(size) - 10
    checksum = 0x1234
    complement = checksum ^ 0xffff if valid else 0
    header[0x2c:0x2e] = complement.to_bytes(2, 'little')
    header[0x2e:0x30] = checksum.to_bytes(2, 'little')
    offset = 0x7fb0 if mapping == 'lo' else 0xffb0
    rom[offset:offset + 80] = header
    return bytes(rom), bytes(header)


class SnesMemoryBus:
    def __init__(self, rom, mapping):
        self.rom = rom
        self.mapping = mapping
        self.calls = []

    def read(self, mode, address, size):
        self.calls.append((mode, address, size))
        if mode != 'snes-rom':
            raise AssertionError(mode)
        bank, offset = address >> 16, address & 0xffff
        if self.mapping == 'lo':
            if offset < 0x8000:
                raise AssertionError('LoROM ROM reads must use 8000-FFFF')
            base = bank * 32768 if bank < 0x80 else (bank - 0x80) * 32768
            physical = base + (offset - 0x8000)
        else:
            if bank == 0:
                physical = offset  # header at bank 00:FFB0
            elif bank >= 0xc0:
                physical = (bank - 0xc0) * 65536 + offset
            else:
                raise AssertionError('Unexpected HiROM bank')
        result = self.rom[physical:physical + size]
        if len(result) != size:
            raise AssertionError('Test requested out-of-range ROM bytes')
        return result


class TestSnesReader(unittest.TestCase):
    def test_oscr_header_offsets_and_database_identity(self):
        rom, header = make_rom()
        bus = SnesMemoryBus(rom, 'lo')
        record = {'name': 'Synthetic LoROM', 'checksum': '1234',
                  'id32': reader.crc(header), 'crc32': reader.crc(rom),
                  'size': len(rom), 'banks': 16}
        info, candidates = reader.detect_snes(bus, [record])
        self.assertTrue(info['valid_header'])
        self.assertTrue(info['checksum_complement_valid'])
        self.assertEqual(info['mapping'], 'lo')
        self.assertEqual(info['header_size_bytes'], 524288)
        self.assertEqual(info['title'], 'TEST GAME')
        self.assertEqual(info['candidate_count'], 1)
        self.assertEqual(candidates[0]['name'], record['name'])
        self.assertEqual(bus.calls[0], ('snes-rom', 0xffb0, 80))

    def test_lorom_dump_matches_oscr_32k_banks(self):
        rom, _ = make_rom()
        bus = SnesMemoryBus(rom, 'lo')
        self.assertEqual(reader.read_snes(bus, 'lo', len(rom)), rom)
        self.assertEqual(len(bus.calls), 16)
        self.assertEqual(bus.calls[0], ('snes-rom', 0x008000, 32768))
        self.assertEqual(bus.calls[-1], ('snes-rom', 0x0f8000, 32768))

    def test_hirom_dump_matches_oscr_64k_banks(self):
        rom, header = make_rom('hi')
        bus = SnesMemoryBus(rom, 'hi')
        info, _ = reader.detect_snes(bus, [])
        self.assertTrue(info['valid_header'])
        self.assertEqual(info['mapping'], 'hi')
        self.assertEqual(reader.read_snes(bus, 'hi', len(rom)), rom)
        self.assertEqual(bus.calls[1], ('snes-rom', 0xc00000, 65536))
        self.assertEqual(bus.calls[-1], ('snes-rom', 0xc70000, 65536))

    def test_lorom_upper_banks_and_validation(self):
        addresses, bank_size = reader.snes_bank_plan('lo', 4194304)
        self.assertEqual(len(addresses), 128)
        self.assertEqual(bank_size, 32768)
        self.assertEqual(addresses[0], 0x808000)
        self.assertEqual(addresses[-1], 0xff8000)
        addresses, bank_size = reader.snes_bank_plan('hi', 4194304)
        self.assertEqual((addresses[0], addresses[-1], bank_size),
                         (0xc00000, 0xff0000, 65536))
        for mapping, size in [('lo', 0), ('hi', 32768), ('lo', 4194305),
                              ('superfx', 524288)]:
            with self.assertRaises(reader.ReadError):
                reader.snes_bank_plan(mapping, size)

    def test_reject_special_chip_and_invalid_header(self):
        rom, _ = make_rom(chips=0x13)
        info, _ = reader.detect_snes(SnesMemoryBus(rom, 'lo'), [])
        self.assertFalse(info['valid_header'])
        self.assertEqual(info['chips'], 0x13)
        rom, _ = make_rom(valid=False)
        info, _ = reader.detect_snes(SnesMemoryBus(rom, 'lo'), [])
        self.assertFalse(info['checksum_complement_valid'])

    def test_cli_auto_dump_database_crc_and_no_copier_header(self):
        rom, header = make_rom('hi')
        bus = SnesMemoryBus(rom, 'hi')
        record = {'name': 'Synthetic HiROM', 'checksum': '1234',
                  'id32': reader.crc(header), 'crc32': reader.crc(rom),
                  'size': len(rom), 'banks': 8}

        class DummySlot:
            def __init__(self, system):
                if system != 'snes':
                    raise AssertionError(system)
            def __enter__(self):
                return bus
            def __exit__(self, *args):
                pass

        with tempfile.TemporaryDirectory() as tmp:
            db = Path(tmp) / 'snes.json'
            db.write_text(json.dumps({'records': [record]}), encoding='utf-8')
            target = Path(tmp) / 'test.sfc'
            out = io.StringIO()
            with patch.object(reader, 'Slot', DummySlot), redirect_stdout(out):
                rc = reader.main(['--system', 'snes', '--action', 'read-rom',
                                  '--database', str(db), '--output', str(target)])
            self.assertEqual(rc, 0)
            self.assertEqual(target.read_bytes(), rom)
            result = json.loads(out.getvalue())
            self.assertTrue(result['verified'])
            self.assertEqual(result['mapping_used'], 'hi')
            self.assertEqual(result['database_matches'], ['Synthetic HiROM'])
            self.assertEqual(result['bytes_written'], len(rom))
            self.assertEqual(result['dump_crc32'], reader.crc(rom))

    def test_real_kernel_seek_mode_zero_even_for_hirom(self):
        with patch.object(reader.os, 'open', return_value=42), \
             patch.object(reader.os, 'lseek', return_value=0) as seek, \
             patch.object(reader.os, 'read', return_value=b'ABC'), \
             patch.object(reader.os, 'close'):
            with reader.Slot('snes') as bus:
                self.assertEqual(bus.read('snes-rom', 0xc00000, 3), b'ABC')
        seek.assert_called_once_with(42, 0xc00000, reader.os.SEEK_SET)

    def test_invalid_modes_and_address_overflow_are_blocked(self):
        with patch.object(reader.os, 'open', return_value=42), \
             patch.object(reader.os, 'close'):
            with reader.Slot('snes') as bus:
                with self.assertRaises(reader.ReadError):
                    bus.read('snes-rom', 0xfffff0, 32)
                with self.assertRaises(reader.ReadError):
                    bus.read('snes-hirom-mode9', 0xc00000, 1)


if __name__ == '__main__':
    unittest.main()
