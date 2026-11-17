# SNES read-only acceptance on Retrode3

Status: Mock-based tests pass; **not yet tested with a real SNES cartridge**.
This is a Linux reader port of the new OSCR SNES header and bank algorithms,
not yet a native compilation of the rewritten upstream C++ cartridge core.

## Architecture and provenance

- Original algorithm: third_party/oscr-firmware/src/cores/SNES.cpp
  (checkcart, readLoRomBanks, readHiRomBanks, readROM). The submodule is unmodified.
- Original database: third_party/oscr-firmware/databases/data/snes.json
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

Run from the feature/snes-oscr-readonly checkout. On the embedded Debian 8
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

Then test a known standard HiROM with a different, unused output path.
For a known plain cartridge with a damaged header, the explicit override
syntax is:

~~~sh
python3 tools/retrode3-readtest.py --system snes --action read-rom \
  --snes-mapping lo --size-bytes 524288 --output /tmp/snes-manual.sfc
~~~

This is an example only, not a recommended mapping/size for unknown carts.

Before merging: verify at least two identical dumps per cart, CRC32 against
OSCR or an independently trusted reference, a LoROM and HiROM test,
kernel log, plus NES NROM and MegaDrive regressions.

Deferred: complete native OSCR C++ core port, special-chip mapping,
SRAM, flash, save restore, web UI dispatch and performance optimization.
