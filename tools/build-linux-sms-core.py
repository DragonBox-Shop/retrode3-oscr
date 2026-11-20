#!/usr/bin/env python3
"""Generate the limited Linux SMS reader from exact pinned Sanni SMS.cpp.

The two extracted algorithms are ORIGINAL upstream size-table switch and
readROM bank/buffer loops. Platform changes are exact, audited edits:
AVR byte reads -> one typed 512-byte Linux HAL operation; SD/UI -> host IO;
mapper register writes -> reviewed HAL method; linear ROMs skip writes.
Any upstream SMS.cpp byte change fails closed until explicitly reviewed.
Python 3.4 compatible. The upstream submodule is never modified.
"""
import argparse
import hashlib
import io
import os
import sys

PINNED_SMS_BLOB = 'e28a1903fa20d2501721ec07935ad766d1d96a48'
UPSTREAM = 'third_party/oscr-firmware/src/cores/SMS.cpp'

SIZE_START = '    switch (cartNib)\n    {\n'
SIZE_END = '\n    // Get rom name'
BANK_SIZE_START = '    // For carts not using mappers (SG1000 or SMS/GG 32 KiB)\n'
BANK_SIZE_END = '\n    OSCR::UI::ProgressBar::init'
READ_START = '    for (uint8_t currBank = 0x0; currBank < (cartSize / bankSize); currBank++)\n    {\n'
READ_END = '\n    cartOff(); // Disable power'

BYTE_LOOP = '''        for (uint16_t currByte = 0; currByte < 512; currByte++)
        {
          OSCR::Storage::Shared::buffer[currByte] = readByte(((adapterMode == SMSMode::SG1000Raphnet) || (cartSize == 32768) ? 0 : 0x8000) + currBuffer + currByte);
        }
        OSCR::Storage::Shared::writeBuffer();'''
BULK_LOOP = '''        // AVR byte loop translated to one typed Linux HAL transaction.
        std::uint8_t buffer[512];
        const std::uint16_t addr = static_cast<std::uint16_t>(
            (banked ? 0x8000u : 0u) + currBuffer);
        if (bus.cancelled()) throw std::runtime_error("cancelled");
        if (!bus.read(addr, buffer, sizeof(buffer)))
          throw std::runtime_error("SMS original ROM read failed");
        if (write(context, buffer, sizeof(buffer)))
          throw std::runtime_error("SMS host output rejected data");'''


def unique_fragment(source, start, end):
    if source.count(start) != 1:
        raise ValueError('OSCR SMS marker changed or duplicated: ' + start.strip())
    start_pos = source.index(start)
    end_pos = source.find(end, start_pos + len(start))
    if end_pos < 0:
        raise ValueError('OSCR SMS end marker changed: ' + end.strip())
    return source[start_pos:end_pos]


def one_replace(text, old, new, name):
    if text.count(old) != 1:
        raise ValueError('OSCR SMS reviewed boundary changed: ' + name)
    return text.replace(old, new)


def extract(source):
    # Intentionally verify the ENTIRE source blob: a new upstream core must
    # receive a review before it can change bus order or cartridge logic.
    encoded = source.encode('utf-8')
    blob = b'blob ' + str(len(encoded)).encode('ascii') + b'\0' + encoded
    if hashlib.sha1(blob).hexdigest() != PINNED_SMS_BLOB:
        raise ValueError('Pinned OSCR SMS.cpp changed; review source and overlays')
    size_block = unique_fragment(source, SIZE_START, SIZE_END).rstrip()
    bank_block = unique_fragment(source, BANK_SIZE_START, BANK_SIZE_END).rstrip()
    reader_block = unique_fragment(source, READ_START, READ_END).rstrip()
    if 'case 0xc:' not in size_block or 'case 0xf:' not in size_block:
        raise ValueError('OSCR SMS size decoder changed')
    bank_block = one_replace(
        bank_block,
        '((adapterMode == SMSMode::SG1000Raphnet) || (cartSize == 32768))',
        '(cartSize <= 32768u)', 'linear 8/16/32 KiB selection')
    reader_block = one_replace(reader_block,
        'if (adapterMode != SMSMode::SG1000Raphnet)', 'if (banked)',
        'banked mapper select gate')
    reader_block = one_replace(reader_block,
        'writeByte(0xFFFF, currBank);',
        'if (!bus.select_page2(currBank))\n          throw std::runtime_error("SMS mapper writes not approved");',
        'mapper write HAL boundary')
    reader_block = one_replace(reader_block, BYTE_LOOP, BULK_LOOP,
                               'AVR 512-byte ROM read boundary')
    reader_block = one_replace(reader_block,
        '      OSCR::UI::ProgressBar::advance(); // Advance progress bar',
        '      // Progress comes from the shared CLI output callback.',
        'host progress boundary')
    if 'readByte(' in reader_block or 'writeByte(' in reader_block:
        raise ValueError('OSCR SMS unmapped per-byte hardware operation')

    sizes = '''// GENERATED FROM PINNED ORIGINAL OSCR src/cores/SMS.cpp. DO NOT EDIT.
// Original getCartInfo ROM-size switch, with no algorithm substitutions.
#ifndef RETRODE3_GENERATED_OSCR_SMS_SIZE_H
#define RETRODE3_GENERATED_OSCR_SMS_SIZE_H
inline std::uint32_t oscr_sms_size_hint(std::uint8_t cartNib)
{
    std::uint32_t cartSize = 0u;
%s
    return cartSize;
}
#endif
''' % size_block

    reader = '''// GENERATED FROM PINNED ORIGINAL OSCR src/cores/SMS.cpp. DO NOT EDIT.
// Original readROM bank and 512-byte buffer loops; platform boundaries only.
#ifndef RETRODE3_GENERATED_OSCR_SMS_READ_H
#define RETRODE3_GENERATED_OSCR_SMS_READ_H
inline void stream_rom(Bus &bus, Layout layout, void *context, WriteFn write)
{
    if (!valid_layout(layout) || !write)
      throw std::runtime_error("invalid SMS read configuration");
    if (!bus.approved()) throw std::runtime_error("SMS adapter bus unapproved");
    const std::uint32_t cartSize = layout.rom_kib * 1024u;
    std::uint32_t bankSize = 16384u; // Original OSCR SMS bank width.
    const bool banked = (layout.mapping == Sega16k);
%s
    bool initialized = false;
    if (banked) {
      if (!bus.begin_sega_banked()) {
        (void)bus.end_sega_banked();
        throw std::runtime_error("SMS mapper init not approved");
      }
      initialized = true;
    }
    try {
%s
    } catch (...) {
      if (initialized) (void)bus.end_sega_banked();
      throw;
    }
    if (initialized && !bus.end_sega_banked())
      throw std::runtime_error("SMS mapper cleanup failed");
}
#endif
''' % (bank_block, reader_block)
    return sizes, reader


def write_file(path, content):
    parent = os.path.dirname(os.path.abspath(path))
    if not os.path.isdir(parent):
        os.makedirs(parent)
    with io.open(path, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write(content)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', default=UPSTREAM)
    parser.add_argument('--size-output', default='build/migration/oscr_sms_size.h')
    parser.add_argument('--read-output', default='build/migration/oscr_sms_read.h')
    args = parser.parse_args(argv)
    with io.open(args.upstream, 'r', encoding='utf-8', newline='') as handle:
        source = handle.read()
    sizes, reader = extract(source)
    write_file(args.size_output, sizes)
    write_file(args.read_output, reader)
    print('Generated pinned original SMS size/ROM loops:', args.size_output,
          args.read_output)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as exc:
        sys.stderr.write('OSCR SMS generation aborted: %s\n' % exc)
        sys.exit(2)