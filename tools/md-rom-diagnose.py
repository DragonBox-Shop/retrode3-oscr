#!/usr/bin/env python3
"""Analyze an existing MegaDrive ROM dump without accessing cartridge hardware.
Compatible with Debian 8 / Python 3.4.
"""
from __future__ import print_function

import argparse
import json
import os
import sys
import zlib

SONIC2_CRC = {
    '24AB4C3A': 'Sonic The Hedgehog 2 (World)',
    '7B905383': 'Sonic The Hedgehog 2 (World) (Rev A)',
    'F23AD4B3': 'Sonic The Hedgehog 2 (World) (Rev B)'
}


def crc32(data):
    return '{:08X}'.format(zlib.crc32(data) & 0xffffffff)


def text(data, start, end):
    return data[start:end].decode('ascii', 'replace').replace('\x00', ' ').strip()


def checksum(data):
    total = 0
    for index in range(0x200, len(data)-1, 2):
        total = (total + (data[index] << 8) + data[index+1]) & 0xffff
    return total


def swapped_words(data):
    result = bytearray(data)
    even = len(result) & ~1
    result[0:even:2] = data[1:even:2]
    result[1:even:2] = data[0:even:2]
    return result


def analyze(data):
    if len(data) < 0x200 or len(data) > 0x1000000:
        raise ValueError('ROM must contain 512 bytes to 16 MiB')
    header_end = int.from_bytes(data[0x1a4:0x1a8], 'big')
    expected_checksum = int.from_bytes(data[0x18e:0x190], 'big')
    calculated = checksum(data)
    swapped_crc = crc32(swapped_words(data))
    actual_crc = crc32(data)
    half = len(data) // 2
    quarter = len(data) // 4
    blocks = [crc32(data[i:i+65536]) for i in range(0,len(data),65536)]
    return {
        'sega_header': data[0x100:0x104] == b'SEGA',
        'domestic_title': text(data,0x120,0x150),
        'overseas_title': text(data,0x150,0x180),
        'serial': text(data,0x180,0x18e),
        'size_bytes': len(data),
        'header_size_bytes': header_end+1,
        'size_matches_header': header_end+1 == len(data),
        'crc32': actual_crc,
        'sonic2_reference_match': SONIC2_CRC.get(actual_crc),
        'pair_swapped_crc32': swapped_crc,
        'pair_swapped_sonic2_match': SONIC2_CRC.get(swapped_crc),
        'header_checksum': '{:04X}'.format(expected_checksum),
        'calculated_checksum': '{:04X}'.format(calculated),
        'header_checksum_matches': expected_checksum == calculated,
        'halves_identical': len(data)%2==0 and data[:half]==data[half:],
        'quarters_identical': len(data)%4==0 and all(data[0:quarter] == data[i*quarter:(i+1)*quarter] for i in range(1,4)),
        '64k_block_crc32': blocks,
        '64k_distinct_block_count': len(set(blocks))
    }


def compare(a, b):
    if len(a) != len(b):
        return {'same': False, 'size_a': len(a), 'size_b': len(b)}
    count = 0
    first = None
    for index in range(len(a)):
        if a[index] != b[index]:
            count += 1
            if first is None:
                first = index
    return {'same': count == 0, 'different_bytes': count, 'first_differing_offset': first}


def main(args=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', help='Existing MegaDrive .bin file')
    parser.add_argument('--compare', help='Second MegaDrive dump for byte-wise comparison')
    arguments = parser.parse_args(args)
    try:
        with open(arguments.file, 'rb') as handle:
            data = handle.read()
        report = analyze(data)
        report['file'] = os.path.abspath(arguments.file)
        if arguments.compare:
            with open(arguments.compare, 'rb') as handle:
                other = handle.read()
            report['comparison'] = compare(data, other)
            report['comparison_crc32'] = crc32(other)
        print(json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True))
        return 0
    except (IOError, OSError, ValueError) as error:
        print(json.dumps({'ok': False, 'error': str(error)}), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
