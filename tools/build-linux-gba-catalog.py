#!/usr/bin/env python3
"""Offline index from pinned Sanni GBA catalog, no firmware modifications."""
import hashlib
import io
import json
import os
import re

SOURCE = 'third_party/oscr-gba-db.json'
BLOB = '83cdad5298788db2b5540e13681a1e726b9050d7'
def literal(s):
    return '"' + ''.join(chr(b) if 32 <= b < 127 and b not in (34, 92) else '\\%03o' % b
                         for b in s.encode('utf-8')) + '"'
with io.open(SOURCE, 'r', encoding='utf-8', newline='') as f:
    raw = f.read().encode('utf-8')
if hashlib.sha1(b'blob ' + str(len(raw)).encode('ascii') + b'\0' + raw).hexdigest() != BLOB:
    raise ValueError('Pinned GBA catalog changed')
records = json.loads(raw.decode('utf-8'))['records']
lines = []
for rec in records:
    serial, crc, size, name = rec['serial'], rec['crc32'], rec['size'], rec['name']
    if not isinstance(size, int) or size < 0 or size > 64 or not re.match(r'^[0-9A-Fa-f]{8}$', crc):
        raise ValueError('Invalid GBA catalog geometry or CRC')
    lines.append('    {%s, %uu, 0x%su, %s},\n' %
                 (literal(serial), size, crc, literal(name)))
if not os.path.isdir('build/migration'):
    os.makedirs('build/migration')
with io.open('build/migration/oscr_gba_catalog.h', 'w', encoding='utf-8') as f:
    f.write('/* Generated from pinned Sanni GBA catalog. */\n'
            'struct GbaRecord { const char *serial; unsigned mib; uint32_t crc; const char *title; };\n'
            'static const GbaRecord gba_catalog[] = {\n' + ''.join(lines) + '};\n')
