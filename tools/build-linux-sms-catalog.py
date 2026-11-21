#!/usr/bin/env python3
"""Generate SMS CRC32 lookup from a pinned upstream OSCR database snapshot.

Source: oscartreader/oscr-databases/data/sms.json
Commit: 58ee0dd4770d5618d55efdd84ef355edbefcae99
Do not modify upstream Sanni firmware or invent mapper metadata. The source
contains full-ROM CRC32 and titles ONLY, not mapper or size fingerprints.
Python 3.4-compatible and completely offline.
"""
import argparse
import hashlib
import io
import json
import os
import sys

SOURCE = 'third_party/oscr-sms-db.json'
BLOB = 'c34d43076180413cb7857a1db0835c4559ac6c7d'


def cpp_literal(value):
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
    chunks.append('"')
    return ''.join(chunks)


def build(src):
    raw = src.encode('utf-8')
    header = b'blob ' + str(len(raw)).encode('ascii') + b'\0'
    actual = hashlib.sha1(header + raw).hexdigest()
    if actual != BLOB:
        raise ValueError('pinned SMS database changed; review and update snapshot')
    catalog = json.loads(src)
    if catalog.get('system') != 'SMS':
        raise ValueError('not the OSCR SMS catalog')
    entries = []
    for item in catalog['records']:
        crc = item['crc32'].upper()
        name = item['name']
        if len(crc) != 8 or any(c not in '0123456789ABCDEF' for c in crc):
            raise ValueError('bad SMS CRC32')
        if not isinstance(name, str) or not name or len(name) > 255:
            raise ValueError('bad SMS catalog title')
        entries.append((int(crc, 16), name))
    entries.sort()
    if len(entries) != len(set(i[0] for i in entries)):
        raise ValueError('ambiguous SMS database CRC32; manual review required')
    rows = ''.join('    {0x%08Xu, %s},\n' % (crc, cpp_literal(name))
                   for crc, name in entries)
    return '''// GENERATED from pinned oscartreader/oscr-databases/data/sms.json.
// Full ROM CRC32 reference ONLY; no mapper geometry or fast fingerprints.
// Do not edit. Regenerate with tools/build-linux-sms-catalog.py.
#ifndef RETRODE3_GENERATED_OSCR_SMS_CATALOG_H
#define RETRODE3_GENERATED_OSCR_SMS_CATALOG_H
#include <cstddef>
#include <cstdint>
namespace retrode3 { namespace sms {
struct CatalogEntry { std::uint32_t crc32; const char *title; };
static const CatalogEntry kCatalog[] = {
%s};
inline const CatalogEntry *catalog_lookup(std::uint32_t crc)
{
    std::size_t lo = 0;
    std::size_t hi = sizeof(kCatalog) / sizeof(kCatalog[0]);
    while (lo < hi) {
        const std::size_t m = lo + (hi - lo) / 2u;
        if (kCatalog[m].crc32 < crc) lo = m + 1;
        else hi = m;
    }
    if (lo < sizeof(kCatalog) / sizeof(kCatalog[0]) &&
        kCatalog[lo].crc32 == crc) return &kCatalog[lo];
    return NULL;
}
} } // namespace retrode3::sms
#endif
''' % rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', default=SOURCE)
    parser.add_argument('--output', default='build/migration/oscr_sms_catalog.h')
    args = parser.parse_args()
    with io.open(args.input, 'r', encoding='utf-8', newline='') as handle:
        code = build(handle.read())
    parent = os.path.dirname(args.output)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    with io.open(args.output, 'w', encoding='utf-8', newline='\n') as out:
        out.write(code)
    print('Generated pinned OSCR SMS CRC32 catalog:', args.output)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as exc:
        sys.stderr.write('SMS catalog generation aborted: %s\n' % exc)
        sys.exit(2)
