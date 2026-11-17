#!/usr/bin/env python3
"""Retrode3 read-only acceptance CLI (NES NROM, MegaDrive and standard SNES)."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import zlib

MODES = {'nes-prg': 10, 'nes-chr': 11, 'md-rom': 0, 'md-p10': 1, 'md-p1': 2,
         'snes-rom': 0}
NES_DB = Path(__file__).resolve().parents[1] / 'third_party/oscr-firmware/databases/data/nes.json'
MD_DB = Path(__file__).resolve().parents[1] / 'third_party/oscr-firmware/databases/data/md.json'
SNES_DB = Path(__file__).resolve().parents[1] / 'third_party/oscr-firmware/databases/data/snes.json'


class ReadError(Exception):
    pass


class Slot:
    md_chunk_size = 2048

    def __init__(self, system, path=None):
        if path is not None:
            raise ReadError('Device override is intentionally unsupported')
        self.path = {'nes': '/dev/slot-nes', 'md': '/dev/slot-md',
                     'snes': '/dev/slot-snes'}[system]
        self.fd = None

    def __enter__(self):
        # Matches the existing retrode3-lib open() ABI; this tool never calls write().
        self.fd = os.open(self.path, os.O_RDWR | os.O_CLOEXEC)
        return self

    def __exit__(self, *exc):
        os.close(self.fd)
        self.fd = None

    def read(self, mode, address, size):
        if mode not in MODES:
            raise ReadError('Unknown read-only kernel bus mode')
        limit = 0x10000 if mode.startswith('nes-') else 0x1000000
        if size < 0 or address < 0 or address + size > limit:
            raise ReadError('Read exceeds the cartridge address/bank space')
        if size == 0:
            return b''
        result = bytearray()
        while len(result) < size:
            start = address + len(result)
            os.lseek(self.fd, (MODES[mode] << 24) | start, os.SEEK_SET)
            data = os.read(self.fd, min(size - len(result), 512 if mode.startswith(('nes-', 'snes-'))
                                else self.md_chunk_size))
            if not data:
                raise ReadError('Short read from {} at {:#x}'.format(self.path, start))
            result.extend(data)
        return bytes(result)


def crc(value):
    return '{:08X}'.format(zlib.crc32(value) & 0xffffffff)


def hex_integer(value):
    if isinstance(value, str):
        return int(value, 16)
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    raise ReadError('Database contains an invalid hexadecimal value')


def load_database(path, name):
    if not path.is_file():
        raise ReadError('OSCR {} database not found: {}; initialize nested submodules'.format(name, path))
    with open(str(path), 'r', encoding='utf-8') as database_file:
        data = json.load(database_file)
    if not isinstance(data, dict) or not isinstance(data.get('records'), list):
        raise ReadError('Invalid OSCR {} database'.format(name))
    return data['records']


def nes_header(value):
    if isinstance(value, str):
        try:
            data = bytes.fromhex(value)
        except ValueError as exc:
            raise ReadError('Invalid iNES header in database') from exc
    elif isinstance(value, list):
        data = bytes(value)
    else:
        raise ReadError('Missing original iNES header in OSCR database')
    if len(data) != 16 or data[:4] != b'NES\x1a':
        raise ReadError('Not an iNES header in OSCR database')
    return data


def nrom_layout(header):
    if len(header) != 16 or header[:4] != b'NES\x1a':
        raise ReadError('Invalid NES ROM header')
    nes2 = (header[7] & 0x0c) == 0x08
    mapper = (header[6] >> 4) | (header[7] & 0xf0)
    if nes2:
        mapper |= (header[8] & 0x0f) << 8
        submapper = header[8] >> 4
        # NES 2.0 upper size nibbles of 0xF use an exponent/multiplier
        # encoding and are intentionally unsupported by the NROM test.
        if (header[9] & 0x0f) == 15 or (header[9] >> 4) == 15:
            raise ReadError('NES 2.0 exponent-encoded ROM sizes are unsupported')
        prg_banks = header[4] | ((header[9] & 0x0f) << 8)
        chr_banks = header[5] | ((header[9] >> 4) << 8)
        if submapper != 0:
            raise ReadError('NES 2.0 submapper is unsupported in the NROM test')
    else:
        prg_banks = header[4]
        chr_banks = header[5]
    if mapper != 0 or prg_banks not in (1, 2) or chr_banks not in (0, 1):
        raise ReadError('Only mapper 0, 16/32 KiB PRG and 0/8 KiB CHR are supported in the first test')
    if header[6] & 0x04:
        raise ReadError('Trainer cartridges are unsupported in the first test')
    return prg_banks * 16384, chr_banks * 8192


def manual_nrom_header(args):
    if args.mapper != 0:
        raise ReadError('Only NES mapper 0 is enabled for read testing')
    if args.prg_kib not in (16, 32) or args.chr_kib not in (0, 8):
        raise ReadError('NROM supports 16/32 KiB PRG and 0/8 KiB CHR')
    if args.mirroring is None:
        raise ReadError('Manual NROM mode requires --mirroring horizontal|vertical')
    result = bytearray(16)
    result[:4] = b'NES\x1a'
    result[4], result[5] = args.prg_kib // 16, args.chr_kib // 8
    result[6] = int(args.mirroring == 'vertical')
    return bytes(result)


def detect_nes(bus, database):
    first = bus.read('nes-prg', 0x8000, 512)
    last = bus.read('nes-prg', 0xe000, 512)
    probes = {crc(first), crc(last)}
    if first in (bytes(512), bytes([255]) * 512) and last in (bytes(512), bytes([255]) * 512):
        raise ReadError('NES slot appears empty or unreadable (all 0x00/0xFF)')
    candidates = []
    for row in database:
        keys = {hex_integer(row[k]) for k in ('id32a', 'id32b') if k in row}
        if keys & {int(p, 16) for p in probes}:
            candidates.append(row)
    info = {'first512_crc32': crc(first), 'last512_crc32': crc(last), 'candidates': [
        {'name': r.get('name', ''), 'rom_crc32': '{:08X}'.format(hex_integer(r.get('crc32', 0))),
         'mapper': r.get('mapper'), 'submapper': r.get('submapper')}
        for r in candidates]}
    return info, candidates


def read_nes(bus, header):
    prg_size, chr_size = nrom_layout(header)
    prg = bus.read('nes-prg', 0x8000, prg_size)
    chr_data = bus.read('nes-chr', 0, chr_size)
    return header + prg + chr_data, crc(prg + chr_data)


def md_header_text(header, start, end):
    return header[start:end].decode('ascii', 'replace').replace('\x00', ' ').strip()


def detect_md(bus, mode):
    header = bus.read(mode, 0, 0x200)
    end_address = int.from_bytes(header[0x1a4:0x1a8], 'big')
    size = end_address + 1
    valid = header[0x100:0x104] == b'SEGA' and 0x200 <= size <= 0x1000000
    return {'sega_header': header[0x100:0x104] == b'SEGA',
            'header_size_bytes': size if valid else None, 'valid_header': valid,
            'domestic_title': md_header_text(header, 0x120, 0x150),
            'overseas_title': md_header_text(header, 0x150, 0x180),
            'serial': md_header_text(header, 0x180, 0x18e),
            'region': md_header_text(header, 0x1f0, 0x200)}


# These offsets follow OSCR::Cores::SNES::checkcart() (SNES.cpp):
# its 80-byte header is read from SNES address 00:FFB0.
SNES_MAP_MODES = {0x20: 'lo', 0x30: 'lo', 0x21: 'hi', 0x31: 'hi'}
SNES_STANDARD_CHIPS = (0, 1, 2)  # plain ROM, ROM+RAM, ROM+save RAM
SNES_NONSTANDARD_SIZES = (1310720, 1572864, 2621440, 3145728)


def snes_header_info(header):
    if len(header) != 80:
        raise ReadError('Expected exactly 80 bytes of SNES header')
    mapping_byte = header[0x25]
    chips = header[0x26]
    exponent = header[0x27]
    complement = int.from_bytes(header[0x2c:0x2e], 'little')
    checksum = int.from_bytes(header[0x2e:0x30], 'little')
    mapping = SNES_MAP_MODES.get(mapping_byte)
    size = 1 << (exponent + 10) if 7 <= exponent <= 12 else None
    checksum_valid = (checksum ^ complement) == 0xffff and checksum != 0
    return {
        'title': header[0x10:0x25].decode('ascii', 'replace').replace('\x00', ' ').strip(),
        'mapping_byte': '{:02X}'.format(mapping_byte),
        'mapping': mapping,
        'chips': chips,
        'checksum': '{:04X}'.format(checksum),
        'complement': '{:04X}'.format(complement),
        'checksum_complement_valid': checksum_valid,
        'header_crc32': crc(header),
        'header_size_bytes': size,
        'valid_header': bool(mapping and checksum_valid and size and
                             chips in SNES_STANDARD_CHIPS),
    }


def detect_snes(bus, database):
    # Bank 00:FFB0 is the OSCR probe address for both standard LoROM and HiROM.
    header = bus.read('snes-rom', 0x00ffb0, 80)
    info = snes_header_info(header)
    wanted_crc = int(info['header_crc32'], 16)
    wanted_checksum = int(info['checksum'], 16)
    candidates = [
        row for row in database
        if 'id32' in row and 'checksum' in row
        and hex_integer(row['id32']) == wanted_crc
        and hex_integer(row['checksum']) == wanted_checksum
    ]
    info['candidates'] = [{
        'name': row.get('name', ''),
        'rom_crc32': '{:08X}'.format(hex_integer(row['crc32'])),
        'size_bytes': row.get('size'),
    } for row in candidates[:20]]
    info['candidate_count'] = len(candidates)
    return info, candidates


def snes_bank_plan(mapping, size):
    """Mirror OSCR SNES.cpp readLoRomBanks/readHiRomBanks for plain cartridges."""
    if mapping not in ('lo', 'hi'):
        raise ReadError('SNES mapping must be lo or hi')
    bank_bytes = 32768 if mapping == 'lo' else 65536
    if size < 131072 or size > 4194304 or size % bank_bytes:
        raise ReadError('SNES standard ROM size must be 128 KiB to 4 MiB, '
                        'aligned to the selected bank size')
    count = size // bank_bytes
    # OSCR starts >24 Mbit LoROMs at 0x80 to avoid special-chip windows.
    start = (0x80 if size > 3145728 else 0) if mapping == 'lo' else 0xc0
    if start + count > 256:
        raise ReadError('SNES bank range would wrap past bank FF')
    offset = 0x8000 if mapping == 'lo' else 0
    return [(bank << 16) | offset for bank in range(start, start + count)], bank_bytes


def read_snes(bus, mapping, size):
    addresses, bank_bytes = snes_bank_plan(mapping, size)
    # The kernel exposes the actual 24-bit bus. SNES_HIROM mode 9 is not
    # implemented by retrode3_read(); mode 0 works with BOTH mappings.
    return b''.join(bus.read('snes-rom', address, bank_bytes) for address in addresses)


def save_atomic(path, content):
    path = Path(os.path.expanduser(str(path)))
    if path.exists():
        raise ReadError('Output file already exists: {}'.format(path))
    tmp = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.' + path.name + '.', suffix='.partial',
                                         dir=str(path.parent), delete=False) as f:
            tmp = Path(f.name)
            f.write(content)
            f.flush()
            os.fsync(f.fileno())
        # Atomic, no-overwrite publication on the same filesystem.
        os.link(str(tmp), str(path))
    finally:
        if tmp is not None:
            if os.path.lexists(str(tmp)):
                os.unlink(str(tmp))


def run(args):
    if args.system == 'nes':
        if args.snes_mapping is not None:
            raise ReadError('--snes-mapping requires --system snes')
        if args.mapper is not None and args.match_crc32 is not None:
            raise ReadError('Select either an OSCR candidate CRC32 or a manual mapper override, not both')
        if args.mapper is None and any(v is not None for v in (args.prg_kib, args.chr_kib, args.mirroring)):
            raise ReadError('NES size/mirroring override requires --mapper 0')
        db_path = Path(args.database) if args.database else NES_DB
        with Slot('nes') as bus:
            if args.mapper is None:
                info, candidates = detect_nes(bus, load_database(db_path, 'NES'))
                if args.action == 'probe':
                    return info, 0
                if args.match_crc32 is not None:
                    wanted = args.match_crc32.upper()
                    if len(wanted) != 8 or any(ch not in '0123456789ABCDEF' for ch in wanted):
                        raise ReadError('--match-crc32 must contain exactly eight hex digits')
                    candidates = [r for r in candidates if '{:08X}'.format(hex_integer(r['crc32'])) == wanted]
                if len(candidates) != 1:
                    raise ReadError('OSCR found {} matching records for this selection; use --action probe to inspect candidates and optionally --match-crc32'.format(len(candidates)))
                chosen = candidates[0]
                header = nes_header(chosen.get('header'))
                data, full_crc = read_nes(bus, header)
                expected = '{:08X}'.format(hex_integer(chosen['crc32']))
                verified = full_crc == expected
                info.update({'matched_name': chosen.get('name'), 'expected_crc32': expected,
                             'dump_crc32': full_crc, 'verified': verified})
            else:
                header = manual_nrom_header(args)
                if args.action == 'probe':
                    data = bus.read('nes-prg', 0x8000, 512)
                    return {'first512_crc32': crc(data), 'manual_mapper': args.mapper}, 0
                data, full_crc = read_nes(bus, header)
                info = {'manual_mapper': 0, 'dump_crc32': full_crc, 'verified': None}
    elif args.system == 'snes':
        if args.mapper is not None or args.match_crc32 is not None or any(
                v is not None for v in (args.prg_kib, args.chr_kib, args.mirroring)):
            raise ReadError('NES mapper options require --system nes')
        if args.md_mode != 'rom' or args.md_chunk_size != 2048:
            raise ReadError('MegaDrive read options require --system md')
        db_path = Path(args.database) if args.database else SNES_DB
        records = load_database(db_path, 'SNES')
        with Slot('snes') as bus:
            info, candidates = detect_snes(bus, records)
            if args.action == 'probe':
                return info, 0
            # Header values must be structurally sound unless both manual
            # mapping and size are supplied. Read-only special-chip access is
            # deliberately not offered in this first acceptance stage.
            if info['chips'] not in SNES_STANDARD_CHIPS:
                raise ReadError('SNES special/unknown chip {:02X} is not supported '
                                'in the read-only standard cartridge test'.format(info['chips']))
            if not info['valid_header'] and (args.snes_mapping is None or
                                             args.size_bytes is None):
                raise ReadError('SNES header invalid; explicitly provide both '
                                '--snes-mapping and --size-bytes for a known standard cart')
            mapping = args.snes_mapping or info['mapping']
            size = args.size_bytes if args.size_bytes is not None else info['header_size_bytes']
            # OSCR's alternative configuration corrects 10/12/20/24 Mbit
            # header sizes if its original database identifies the cartridge.
            if args.size_bytes is None and len(candidates) == 1:
                db_size = candidates[0].get('size')
                if isinstance(db_size, int) and db_size in SNES_NONSTANDARD_SIZES:
                    size = db_size
                    info['database_size_override'] = True
            data = read_snes(bus, mapping, size)
            full_crc = crc(data)
            matches = [r.get('name', '') for r in records
                       if hex_integer(r.get('crc32', 0)) == int(full_crc, 16)]
            info.update({'mapping_used': mapping, 'size_bytes': size,
                         'dump_crc32': full_crc, 'database_matches': matches,
                         'verified': True if matches else None,
                         'manual_override': bool(args.snes_mapping or
                                                 args.size_bytes is not None)})
    else:
        if args.snes_mapping is not None:
            raise ReadError('--snes-mapping requires --system snes')
        if args.mapper is not None or args.match_crc32 is not None:
            raise ReadError('NES mapper/database overrides require --system nes')
        mode = 'md-' + args.md_mode
        with Slot('md') as bus:
            bus.md_chunk_size = args.md_chunk_size
            info = detect_md(bus, mode)
            if args.action == 'probe':
                return info, 0
            size = args.size_bytes if args.size_bytes is not None else info['header_size_bytes']
            if size is None:
                raise ReadError('MegaDrive header invalid; use --size-bytes for a manual dump')
            if size < 512 or size > 0x1000000 or size % 2:
                raise ReadError('MegaDrive size must be even and between 512 bytes and 16 MiB')
            data = bus.read(mode, 0, size)
            full_crc = crc(data)
            matches = []
            if MD_DB.is_file():
                matches = [r.get('name', '') for r in load_database(MD_DB, 'MegaDrive')
                           if hex_integer(r.get('crc32', 0)) == int(full_crc, 16)]
            info.update({'size_bytes': size, 'dump_crc32': full_crc,
                         'database_matches': matches, 'verified': True if matches else None})
    if args.output is None:
        raise ReadError('read-rom requires --output')
    target = Path(os.path.expanduser(args.output))
    if info['verified'] is False:
        target = target.with_name(target.name + '.unverified')
    save_atomic(target, data)
    info.update({'output': str(target), 'bytes_written': len(data),
                 'sha256': hashlib.sha256(data).hexdigest()})
    return info, 0 if info['verified'] is not False else 3


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--system', choices=['nes', 'md', 'snes'], required=True)
    p.add_argument('--action', choices=['probe', 'read-rom'], required=True)
    p.add_argument('--output', help='Destination for .nes or .bin (must not exist)')
    p.add_argument('--database', help='Override original OSCR NES/SNES JSON database path')
    p.add_argument('--mapper', type=int, help='Manual NES mapper, currently only 0')
    p.add_argument('--match-crc32', help='Select an OSCR NES database candidate by its full-ROM CRC32')
    p.add_argument('--prg-kib', type=int)
    p.add_argument('--chr-kib', type=int)
    p.add_argument('--mirroring', choices=['horizontal', 'vertical'])
    p.add_argument('--snes-mapping', choices=['lo', 'hi'],
                   help='Known standard SNES mapping override; use with --size-bytes for bad headers')
    p.add_argument('--md-mode', choices=['rom', 'p10', 'p1'], default='rom')
    p.add_argument('--md-chunk-size', type=int, choices=[2, 512, 1024, 2048], default=2048,
                   help='Read size in bytes (1024 matches the legacy OSCR MegaDrive bulk reader)')
    p.add_argument('--size-bytes', type=int)
    args = p.parse_args(argv)
    try:
        info, code = run(args)
        result = {'ok': code == 0, 'system': args.system, 'action': args.action}
        result.update(info)
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        return code
    except (ReadError, OSError, ValueError, KeyError, TypeError) as exc:
        print(json.dumps({'ok': False, 'error': str(exc)}, ensure_ascii=False), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
