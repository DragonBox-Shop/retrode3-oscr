# SNES read-only acceptance on Retrode3

Status: Mock-based tests pass. **Both standard LoROM and HiROM have passed
physical hardware testing** on Retrode3 2.9.5. LoROM repeatability was
confirmed with two bit-identical dumps; repeat HiROM read remains outstanding.
This is a Linux reader port of the new OSCR SNES header and bank algorithms,
not yet a native compilation of the rewritten upstream C++ cartridge core.

## Architecture and provenance

- Original algorithm: third_party/oscr-firmware/src/cores/SNES.cpp
  (checkcart, readLoRomBanks, readHiRomBanks, readROM). The submodule is unmodified.
- Original database: third_party/oscr-firmware/databases/data/snes.json
  Note: source `size` entries are BIT counts. The CLI reports both
  `size_bits` and the converted `size_bytes`, and compares byte counts
  when applying OSCR 10/12/20/24 Mbit overrides.
- 80-byte SNES header probe at physical address 00:FFB0.
- LoROM: 32 KiB per bank, bank:8000, starting at bank 00 for up to 24 Mbit
  and bank 80 for larger 32 Mbit ROMs, following OSCR.
- HiROM: 64 KiB per bank, bank:0000, starting at bank C0, following OSCR.
- Kernel mode 0 (raw 8-bit bus) is used for BOTH LoROM and HiROM, because
  the present kernel defines SNES_HIROM mode 9 but does not implement it.
  The different physical banks and offsets determine the ROM mapping.
- Supported sizes: 128 KiB to 4 MiB, aligned to 32 KiB (LoROM) or
  64 KiB (HiROM). The OSCR database may correct 10/12/20/24 Mbit headers
  when a unique header identity is found.
- Database identification: header CRC32 matches OSCR id32 AND checksum,
  then full ROM CRC32 is checked. No header/database match means verified: null.
  Recognized header but mismatching full-ROM CRC means verified: false, exit
  code 3, and an output filename ending in .unverified.
- Output: raw headerless .sfc with atomic no-overwrite publication.

## Verified real-cartridge results (2026-11-17)

User-tested on Retrode3 2.9.5, Linux kernel 7.3.0-rc5-letux-mips-retrode3+,
with the read-only Linux SNES CLI:

| Property | Result |
| --- | --- |
| Cartridge | Super Turrican (Europe) |
| Mapping | LoROM, mapping byte 0x20 |
| Header check | checksum 0xEFBA, complement 0x1045, valid |
| Header CRC32 | 3B095057 |
| OSCR database candidates | 1 |
| Chips | 0 (ROM only) |
| Size | 4194304 bits / 524288 bytes, 16 x 32768-byte banks |
| Full dump CRC32 | D0A1BF12 (matches pinned OSCR SNES database) |
| SHA256 | 1bc2b230d6bb6091412fcc9b957192d9a729496f9f2123449d968afb088fc525 |
| CLI | ok: true, verified: true, manual_override: false |
| Destination | /tmp/super-turrican-europe.sfc |

Two successive Super Turrican reads were also confirmed byte-for-byte identical
by the user with cmp; the SHA256 above refers to the first dump.

### HiROM: Terranigma (Germany) (Rev 1)

| Property | Result |
| --- | --- |
| Cartridge | Terranigma (Germany) (Rev 1) |
| Mapping | HiROM, mapping byte 0x31 |
| Header check | checksum 0x3856, complement 0xC7A9, valid |
| Header CRC32 | A32F1525 |
| OSCR database candidates | 1 |
| Chips | 2 (ROM + save RAM; save RAM not accessed) |
| Size | 33554432 bits / 4194304 bytes, 64 x 65536-byte banks |
| Full dump CRC32 | 6FB8A4DF (matches pinned OSCR SNES database) |
| SHA256 | 613cd1a31eaded18648168bd7453a57830ca9a6f3c10de5154625436fbd49556 |
| CLI | ok: true, verified: true, manual_override: false |
| Destination | /tmp/terranigma-germany-rev1.sfc |

Both standard SNES ROM mapping modes have therefore passed a full-dump CRC32
check against the original OSCR database. A second Terranigma dump,
kernel-log inspection and further edge-case testing remain outstanding.
These results do not establish native Linux compilation of the upstream
OSCR C++ SNES core, and do not validate save RAM reading or writing.

## Safety and limitations

Only chip types 0 (ROM only), 1 (ROM + RAM), 2 (ROM + save RAM) are
allowed. Special chips such as DSP, SA-1, SuperFX, CX4, S-DD1, SPC7110
and ExHiROM are explicitly blocked. No SRAM access, writes, clock or
mapper register commands are available in this first acceptance test.

Invalid header/mapping requires a KNOWN normal cartridge and both
--snes-mapping and --size-bytes overrides. Do not guess these values.
The driver still prints some per-address diagnostics and can be slow.

## Development tests

~~~sh
python3 -m unittest discover -s tests -p 'test_*readtest.py' -v
make -f Makefile.migration test-read-bus
~~~

Both are also run by GitHub Actions with mock kernel-bus implementations;
these do not validate electrical timing on physical hardware.

## Physical Retrode3 acceptance

Run from the migration/oscr-firmware checkout. On the embedded Debian 8
system, Python 3.4 is supported. Ensure the pinned nested OSCR databases
submodule is populated. Start with a known, plain LoROM.

~~~sh
test -s third_party/oscr-firmware/databases/data/snes.json

python3 tools/retrode3-readtest.py --system snes --action probe

# Inspect mapping, chips, title and header_size_bytes in the JSON first.
python3 tools/retrode3-readtest.py --system snes --action read-rom \
  --output /tmp/snes-first.sfc

sha256sum /tmp/snes-first.sfc
~~~

A known standard HiROM has been successfully tested (Terranigma); repeat
its read to confirm bit-for-bit consistency with a different unused output path.
For a known plain cartridge with a damaged header, the explicit override
syntax is:

~~~sh
python3 tools/retrode3-readtest.py --system snes --action read-rom \
  --snes-mapping lo --size-bytes 524288 --output /tmp/snes-manual.sfc
~~~

This is an example only, not a recommended mapping/size for unknown carts.

Before finishing SNES acceptance: verify at least two identical dumps per cart, CRC32 against
OSCR or an independently trusted reference, a LoROM and HiROM test,
kernel log, plus NES NROM and MegaDrive regressions.

Deferred: complete native OSCR C++ core port, special-chip mapping,
SRAM, flash, save restore, web UI dispatch and performance optimization.
