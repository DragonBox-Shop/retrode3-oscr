# GBA-MD OSCR source-derived v3 core (development only)

The pinned OSCR GameBoyAdvance.cpp is untouched. The build checks its full
Git blob SHA and extracts the original header-read loop, logo sum, header
checksum and 512-byte ROM loop. The platform overlay translates AVR/SD/UI
operations to the existing read-only GBx kernel HAL (mode 24 for header,
mode 28 for burst data) and common host IO. The former standalone GBx
reader is retained only as a bench/reference tool, not linked to gba-md.so.

The dynamic manifest identifies the physical MD slot. The v3 hardware
profile requests 3300 mV and GBx mode for both probe and read. Shared host
lock and scope are retained. Use only with kernel feature/gbx-scoped-bus until physical acceptance.
The generic slot probe supports opt-in probe_without_sense at 3300 mV.
No Python arm helper is intended in production. No save or flash writes.
32 MiB layouts are enabled on this test branch for physical acceptance.
The HAL maps the lower 16 MiB to modes 24/28 and the upper 16 MiB to
modes 25/29, without changing the original OSCR stream loop. Full 32 MiB
CRC verification on a known cartridge remains required before release. Pinned Sanni GBA database (oscartridge serial, ROM size, full CRC32)
provides candidate selection, auto size for unambiguous layouts and full
CRC verification. Unknown cartridges retain manual size selection.

Build: make -f Makefile.migration native-gba-md-module
Test: make -f Makefile.migration test-native-gba-md-module
Stage (explicit only): make -f Makefile.migration stage-gba-md-module

No actual hardware acceptance is implied by host tests.
