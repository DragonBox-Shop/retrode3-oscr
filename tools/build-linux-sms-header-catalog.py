#!/usr/bin/env python3
"""Offline-generated SMS header candidate index from pinned MAME serial metadata.

Sanni's pinned oscr-sms-db.json remains the sole source for display titles
and full-ROM CRC32 verification. CC0 MAME serials are only PROBE HINTS.
Never infer verified identity or electrical mapper authorization from a
product number; revisions and regions may share the same code.
Python 3.4 compatible; no ROM files or network required.
"""
import argparse
import io
import json
import os
import sys

SOURCE = 'third_party/oscr-sms-product-codes.json'
NAMES = 'third_party/oscr-sms-db.json'
MAME_BLOB = '5410f81bc8e1900375d076f2f7ee42ee852c2c48'


def literal(value):
    chunks = ['"']
    for v in value.encode('utf-8'):
        if v == 34:
            chunks.append('\\\"')
        elif v == 92:
            chunks.append('\\\\')
        elif 32 <= v < 127:
            chunks.append(chr(v))
        else:
            chunks.append('\\%03o' % v)
    return ''.join(chunks) + '"'


def generate(serials, names):
    if (serials.get('format') != 'retrode3-sms-header-product-code-v1' or
            serials.get('source', {}).get('blob_sha') != MAME_BLOB or
            serials['source'].get('license') != 'CC0-1.0'):
        raise ValueError('unreviewed MAME serial metadata source')
    if names.get('system') != 'SMS':
        raise ValueError('not the pinned OSCR SMS database')
    by_crc = {}
    for entry in names['records']:
        crc = entry['crc32'].upper()
        if crc in by_crc:
            raise ValueError('ambiguous OSCR CRC catalog')
        by_crc[crc] = entry['name']
    rows = []
    entries_seen = set()
    for record in serials['entries']:
        crc = record['crc32'].upper()
        size = record['size_bytes']
        codes = record['product_codes']
        if crc not in by_crc or crc in entries_seen or not isinstance(size, int) or (
                size < 8192 or size > 1048576 or size % 1024):
            raise ValueError('invalid serial CRC/size mapping')
        entries_seen.add(crc)
        if not codes or len(set(codes)) != len(codes):
            raise ValueError('invalid serial list')
        for code in codes:
            if not isinstance(code, int) or code < 1 or code > 159999:
                raise ValueError('invalid SMS product code')
            rows.append((code, size, int(crc, 16), by_crc[crc]))
    rows.sort(key=lambda row: (row[0], row[1], row[2]))
    if not rows or len(rows) > 1200:
        raise ValueError('unexpected SMS header catalog size')
    body = ''.join('    {%uu, %uu, 0x%08Xu, %s},\n' %
                   (code, size, crc, literal(title))
                   for code, size, crc, title in rows)
    return '''// GENERATED: MAME CC0 product code hints joined to pinned OSCR SMS CRC titles.
// These are candidate hints ONLY. Full ROM CRC must verify selected revision.
// Data sources: MAME hash/sms.xml blob %s; OSCR oscr-sms-db.json.
#ifndef RETRODE3_GENERATED_SMS_HEADER_CATALOG_H
#define RETRODE3_GENERATED_SMS_HEADER_CATALOG_H
#include <cstddef>
#include <cstdint>
namespace retrode3 { namespace sms {
struct HeaderCandidate {
    std::uint32_t product_code;
    std::uint32_t size_bytes;
    std::uint32_t full_crc32;
    const char *title;
};
static const HeaderCandidate kHeaderCandidates[] = {
%s};
static const std::size_t kHeaderCandidateCount =
    sizeof(kHeaderCandidates) / sizeof(kHeaderCandidates[0]);
} } // retrode3::sms
#endif
''' % (MAME_BLOB, body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', default=SOURCE)
    parser.add_argument('--names', default=NAMES)
    parser.add_argument('--output', default='build/migration/oscr_sms_header_catalog.h')
    options = parser.parse_args()
    with io.open(options.input, 'r', encoding='utf-8') as stream:
        codes = json.load(stream)
    with io.open(options.names, 'r', encoding='utf-8') as stream:
        names = json.load(stream)
    header = generate(codes, names)
    directory = os.path.dirname(options.output)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)
    with io.open(options.output, 'w', encoding='utf-8', newline='\n') as stream:
        stream.write(header)
    print('Generated tentative SMS header candidate index:', options.output)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError) as exc:
        sys.stderr.write('SMS serial catalog rejected: %s\n' % exc)
        sys.exit(2)
