# NES generated ROM algorithms

The generator extracts the complete pinned Sanni readPRG/readCHR switch
bodies and write_mmc1_byte helper, preserving the original case bodies.
The surrounding AVR UI, file lifecycle and voltage control are not copied:
the existing CLI/adapter owns them. Original dumpBankPRG/CHR loops remain
unchanged; the lower-level buffer reads use the existing typed block HAL.

The build applies patches/nes-generated.json in order with unique exact
anchors. The two MMC1 patches preserve the already tested 512 KiB bank
sequence and explicit CHR mode. The former local MMC1 plan is retained only
as a historical regression test oracle; it is not linked into nes.so.
MMC3 A001 SRAM enable is suppressed in the HAL for approved ROM-only
layouts. UxROM/CNROM preflight fixed-bank conflict values; AxROM resolves
the address in the currently mapped bank. No register operation below
8000 is allowed. Other upstream bus primitives fail closed. Legacy CRC
special cases have no upstream legacy checksum in this adapter and use
zero; 8 KiB special layouts and related mappers remain unapproved.

This is a migration of the ROM algorithms, NOT automatic hardware
approval. The existing supported set 0/1/2/3/4/7 and geometry/battery
gates remain unchanged. Mapper 268 is generated but blocked pending
outer register HAL review, extended NES 2.0 metadata and cartridge tests.
The remaining catalogue expansion is separate; existing IDs are preserved.
Run make -f Makefile.migration test-native-nes-module before hardware
regression, and compare actual dump CRCs. No master promotion before tests.
