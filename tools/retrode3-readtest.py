#!/usr/bin/env python3
"""Retrode3 read-only acceptance-test CLI (NROM NES and standard MegaDrive)."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import zlib

MODES = {'nes-prg': 10, 'nes-chr': 11, 'md-rom': 0, 'md-p10': 1, 'md-p1': 2}
NES_DB = Path(__file__).resolve().parents[1] / 'third_party/oscr-firmware/databases/data/nes.json'
MD_DB = Path(__file__).resolve().parents[1] / 'third_party/oscr-firmware/databases/data/md.json'


class ReadError(Exception):
    pass


class Slot:
    def __init__(self, system, path=None):
        if path is not None:
            raise ReadError('Device override is intentionally unsupported')
        self.path = '/dev/slot-nes' if system == 'nes' else '/dev/slot-md'
        self.fd = None

    def __enter__(self):
        # Matches the existing retrode3-lib open() ABI; this tool never calls write().
        self.fd = os.open(self.path, os.O_RDWR | os.O_CLOEXEC)
        return self

    def __exit__(self, *exc):
        os.close(self.fd)
        self.fd = None

    def read(self, mode, address, size):
        limit = 0x10000 if mode.startswith('nes-') else 0x1000000
        if size < 0 or address < 0 or address + size > limit:
            raise ReadError('Read exceeds the cartridge address/bank space')
        if size == 0:
            return b''
        result = bytearray()
        while len(result) < size:
            start = address + len(result)
            os.lseek(self.fd, (MODES[mode] << 24) | start, os.SEEK_SET)
            data = os.read(self.fd, min(size - len(result), 512 if mode.startswith('nes-') else 2048))
            if not data:
                raise ReadError(f'Short read from {self.path} at {start:#x}')
            result.extend(data)
        return bytes(result)


def crc(value):
    return f'{zlib.crc32(value):08X}'


def hex_integer(value):
    if isinstance(value, str):
        return int(value, 16)
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    raise ReadError('Database contains an invalid hexadecimal value')


def load_database(path, name):
    if not path.is_file():
        raise ReadError(f'OSCR {name} database not found: {path}; initialize nested submodules')
    data = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(data, dict) or not isinstance(data.get('records'), list):
        raise ReadError(f'Invalid OSCR {name} database')
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
    # NES 2.0 uses additional size bits and exponent encoding; do not guess these.
    if (header[7] & 0x0c) == 0x08:
        raise ReadError('NES 2.0 header is not yet supported by the NROM test path')
    mapper = (header[6] >> 4) | (header[7] & 0xf0)
    if mapper != 0 or header[4] not in (1, 2) or header[5] not in (0, 1):
        raise ReadError('Only mapper 0, 16/32 KiB PRG and 0/8 KiB CHR are supported in the first test')
    if header[6] & 0x04:
        raise ReadError('Trainer cartridges are unsupported in the first test')
    return header[4] * 16384, header[5] * 8192


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
        {'name': r.get('name', ''), 'rom_crc32': f'{hex_integer(r.get("crc32", 0)):08X}',
         'mapper': r.get('mapper'), 'submapper': r.get('submapper')}
        for r in candidates]}
    return info, candidates


def read_nes(bus, header):
    prg_size, chr_size = nrom_layout(header)
    prg = bus.read('nes-prg', 0x8000, prg_size)
    chr_data = bus.read('nes-chr', 0, chr_size)
    return header + prg + chr_data, crc(prg + chr_data)


def detect_md(bus, mode):
    header = bus.read(mode, 0, 0x200)
    end_address = int.from_bytes(header[0x1a4:0x1a8], 'big')
    size = end_address + 1
    valid = header[0x100:0x104] == b'SEGA' and 0x200 <= size <= 0x1000000
    return {'sega_header': header[0x100:0x104] == b'SEGA',
            'header_size_bytes': size if valid else None, 'valid_header': valid}


def save_atomic(path, content):
    path = Path(path).expanduser()
    if path.exists():
        raise ReadError(f'Output file already exists: {path}')
    tmp = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.' + path.name + '.', suffix='.partial',
                                         dir=path.parent, delete=False) as f:
            tmp = Path(f.name)
            f.write(content)
            f.flush()
            os.fsync(f.fileno())
        # Atomic, no-overwrite publication on the same filesystem.
        os.link(tmp, path)
    finally:
        if tmp is not None:
            tmp.unlink(missing_ok=True)


def run(args):
    if args.system == 'nes':
        if args.mapper is None and any(v is not None for v in (args.prg_kib, args.chr_kib, args.mirroring)):
            raise ReadError('NES size/mirroring override requires --mapper 0')
        db_path = Path(args.database) if args.database else NES_DB
        with Slot('nes') as bus:
            if args.mapper is None:
                info, candidates = detect_nes(bus, load_database(db_path, 'NES'))
                if args.action == 'probe':
                    return info, 0
                if len(candidates) != 1:
                    raise ReadError(f'OSCR found {len(candidates)} matching games; select a manual NROM override or probe first')
                chosen = candidates[0]
                header = nes_header(chosen.get('iNES', chosen.get('ines')))
                data, full_crc = read_nes(bus, header)
                expected = f'{hex_integer(chosen["crc32"]):08X}'
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
    else:
        if args.mapper is not None:
            raise ReadError('--mapper is only supported with --system nes')
        mode = 'md-' + args.md_mode
        with Slot('md') as bus:
            info = detect_md(bus, mode)
            if args.action == 'probe':
                return info, 0
            size = args.size_bytes if args.size_bytes is not None else info['header_size_bytes']
            if size is None:
                raise ReadError('MegaDrive header invalid; use --size-bytes for a manual dump')
            if size < 512 or size > 0x1000000 or size % 2:
                raise ReadError('MegaDrive size must be even and between 512 bytes and 16 MiB')
            data = bus.read(mode, 0, size)
            info.update({'size_bytes': size, 'dump_crc32': crc(data), 'verified': None})
    if args.output is None:
        raise ReadError('read-rom requires --output')
    target = Path(args.output).expanduser()
    if info['verified'] is False:
        target = target.with_name(target.name + '.unverified')
    save_atomic(target, data)
    info.update({'output': str(target), 'bytes_written': len(data),
                 'sha256': hashlib.sha256(data).hexdigest()})
    return info, 0 if info['verified'] is not False else 3


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--system', choices=['nes', 'md'], required=True)
    p.add_argument('--action', choices=['probe', 'read-rom'], required=True)
    p.add_argument('--output', help='Destination for .nes or .bin (must not exist)')
    p.add_argument('--database', help='Override original OSCR NES JSON database path')
    p.add_argument('--mapper', type=int, help='Manual NES mapper, currently only 0')
    p.add_argument('--prg-kib', type=int)
    p.add_argument('--chr-kib', type=int)
    p.add_argument('--mirroring', choices=['horizontal', 'vertical'])
    p.add_argument('--md-mode', choices=['rom', 'p10', 'p1'], default='rom')
    p.add_argument('--size-bytes', type=int)
    args = p.parse_args(argv)
    try:
        info, code = run(args)
        print(json.dumps({'ok': code == 0, 'system': args.system, 'action': args.action,
                          **info}, ensure_ascii=False, sort_keys=True))
        return code
    except (ReadError, OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(json.dumps({'ok': False, 'error': str(exc)}, ensure_ascii=False), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
