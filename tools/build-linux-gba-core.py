#!/usr/bin/env python3
"""Extract pinned original GBA ROM/header algorithms; map platform IO to HAL.

The entire upstream blob is guarded. Only the AVR/SD/UI boundaries are
translated. No save, flash or 32 MiB padding operations are emitted.
Python 3.4 compatible. The OSCR checkout is never edited.
"""
import argparse
import hashlib
import io
import os
import sys

PINNED_GBA_BLOB = '51362f00bce84abd0e6ed0de97d4b93208864a4f'
UPSTREAM = 'third_party/oscr-firmware/src/cores/GameBoyAdvance.cpp'

def fragment(source, start, end):
    if source.count(start) != 1:
        raise ValueError('GBA upstream marker changed: ' + start.strip())
    pos = source.index(start)
    stop = source.index(end, pos + len(start))
    return source[pos:stop].rstrip()

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('GBA platform boundary changed: ' + old)
    return text.replace(old, new)

def extract(source):
    raw = source.encode('utf-8')
    blob = b'blob ' + str(len(raw)).encode('ascii') + b'\0' + raw
    if hashlib.sha1(blob).hexdigest() != PINNED_GBA_BLOB:
        raise ValueError('Pinned GameBoyAdvance.cpp changed; review HAL overlay')
    checksum = fragment(source, '  uint8_t checksumHeader(uint8_t const * header)', '\n  // Read info out of rom header')
    header = fragment(source, '      for (uint8_t currWord = 0; currWord < 192; currWord += 2)', '\n      cartOff();')
    header = replace_once(header,
        'OSCR::Storage::Shared::putWord(currWord, readWord(currWord), Endian::LSBFirst);',
        'device.read_mode(MODE_GBA_SLOW, currWord, bytes + currWord, 2);')
    logo = fragment(source, '      logoChecksum = 0;', '\n      if (logoChecksum == 0x4B1B) break;')
    logo = replace_once(logo, 'OSCR::Storage::Shared::buffer[currByte]', 'bytes[currByte]')
    loop = fragment(source, '    for (uint32_t myAddress = 0; myAddress < cartSize; myAddress += 512)', '\n    cartOff();')
    loop = replace_once(loop, '''      for (uint16_t currWord = 0; currWord < 512; currWord += 2)
      {
        OSCR::Storage::Shared::putWord(currWord, readWord(myAddress + currWord), Endian::LSBFirst);
      }''', '''      unsigned char buffer[512];
      if (io->cancelled(io->context)) throw std::runtime_error("cancelled");
      device.read_mode(MODE_GBA_BURST, myAddress, buffer, sizeof(buffer));''')
    loop = replace_once(loop, 'OSCR::Storage::Shared::writeBuffer();',
        'if (io->write(io->context, buffer, sizeof(buffer))) throw std::runtime_error("host cancelled or refused GBA output");')
    loop = replace_once(loop, 'OSCR::UI::ProgressBar::advance(512);',
        '// Progress is reported by the shared CLI.')
    return '''// Generated from pinned original OSCR GameBoyAdvance.cpp. Do not edit.
#ifndef RETRODE3_GENERATED_GBA_H
#define RETRODE3_GENERATED_GBA_H
%s

inline void oscr_gba_header(RomDevice &device, unsigned char (&bytes)[192])
{
    uint16_t logoChecksum;
%s
%s
    if (logoChecksum != 0x4B1B || checksumHeader(bytes) != bytes[0xbd])
        throw std::runtime_error("invalid GBA Nintendo logo or header checksum");
}

inline void oscr_gba_stream(RomDevice &device, uint32_t cartSize,
                            const retrode3_host_io_v1 *io)
{
%s
}
#endif
''' % (checksum, header, logo, loop)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', default=UPSTREAM)
    parser.add_argument('--output', default='build/migration/oscr_gba_generated.h')
    args = parser.parse_args()
    with io.open(args.upstream, 'r', encoding='utf-8', newline='') as handle:
        result = extract(handle.read())
    parent = os.path.dirname(os.path.abspath(args.output))
    if not os.path.isdir(parent):
        os.makedirs(parent)
    with io.open(args.output, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write(result)

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as exc:
        sys.stderr.write('OSCR GBA generation aborted: %s\n' % exc)
        sys.exit(2)
