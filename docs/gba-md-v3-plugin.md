# GBA-MD OSCR source-derived v3 core (development only)

The pinned OSCR GameBoyAdvance.cpp is untouched. The build checks its full
Git blob SHA and extracts the original header-read loop, logo sum, header
checksum and 512-byte ROM loop. The platform overlay translates AVR/SD/UI
operations to the existing read-only GBx kernel HAL (mode 24 for header,
mode 28 for burst data) and common host IO. The former standalone GBx
reader is retained only as a bench/reference tool, not linked to gba-md.so.

The dynamic manifest identifies the physical MD slot. The v3 hardware
profile requests 3300 mV and GBx mode for both probe and read. Shared host
lock and scope are retained. Do not enable in normal use until the kernel
implements reversible direction switching, and generic slot discovery
handles GBx adapters with unreliable sense. Both remain outstanding.
No Python arm helper is intended in production. No save or flash writes.
32 MiB layout stays disabled pending hardware verification. Sanni database
integration is still outstanding; manual size selection remains mandatory.

Build: make -f Makefile.migration native-gba-md-module
Test: make -f Makefile.migration test-native-gba-md-module
Stage (explicit only): make -f Makefile.migration stage-gba-md-module

No actual hardware acceptance is implied by host tests.
