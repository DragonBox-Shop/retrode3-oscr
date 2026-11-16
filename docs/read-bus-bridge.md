# Retrode3 read-only cartridge bus bridge

This is a small **read-only HAL foundation**, not a working port of the OSCR firmware. It wraps the existing Retrode3 kernel ABI via \`retrode3-lib.h\` and exposes typed NES, MegaDrive and SNES read operations.

- NES: PRG, CHR, CHR with M2, RAM and WRAM modes.
- MegaDrive: 24-bit **byte** addresses for block reads, **word** addresses for the upstream \`readWord\` API, explicit access mode and big-endian word decoding.
- SNES: 8-bit bank and 16-bit address, explicit regular/HiROM mode.
- Rejects address-space/bank crossings and short reads instead of silently generating corrupt ROMs.

Test on any Linux development machine without cartridge hardware:

\`\`\`sh
make -f Makefile.migration test-read-bus
\`\`\`

The test file provides fake \`nes_read\`, \`md_read\`, \`snes_read\` functions and checks dispatch and address conversions. The real library is **not linked** in that test.

**Not implemented yet:** slot power/voltage lifecycle, integration of new upstream OSCR C++ cores, auto-detect invocation, CLI dispatch, performance optimization, additional mapper/EEPROM protocols, and all cartridge writes (including save restores/flash). These omissions are deliberate: do not treat this HAL as ready for actual cartridges.

The generated web schema in \`docs/web-menu-export.md\` describes potential actions but cannot invoke this HAL directly.
