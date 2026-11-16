# First Retrode3 read-only hardware test

**State:** Experimental hardware-acceptance CLI, **not** the new OSCR C++ core port. It is deliberately separate from the stable \`oscr\` program and exercises the same Linux \`/dev/slot-nes\` / \`/dev/slot-md\` ABI without changing \`master\`. Test with a known expendable cartridge first; no voltage/power setup or save writing has been added here. The existing kernel must already initialize the slots correctly.

Requirements: Python 3 and readable kernel slot devices. From a checked-out **migration branch**:

\`\`\`sh
# The nested OSCR databases submodule uses an SSH URL. Rewrite it temporarily
# if this host has HTTPS but no GitHub SSH credentials:
git submodule update --init third_party/oscr-firmware
git -C third_party/oscr-firmware \
    -c url.https://github.com/.insteadOf=git@github.com: \
    submodule update --init databases

python3 -m unittest discover -s tests -p 'test_readtest.py' -v

# NES cartridge identification, read-only (uses original OSCR data/nes.json)
python3 tools/retrode3-readtest.py --system nes --action probe

# NES mapper 0/NROM: dump with the original detected iNES header, verify CRC32
python3 tools/retrode3-readtest.py --system nes --action read-rom \
    --output /tmp/retrode3-test.nes

# If probe returns multiple candidates, select the chosen original database entry
# by the 'rom_crc32' value in the probe JSON:
python3 tools/retrode3-readtest.py --system nes --action read-rom \
    --match-crc32 12345678 --output /tmp/retrode3-selected.nes

# If recognition fails, try a KNOWN NROM board with explicit parameters
# Only mapper 0 is enabled; manual mode cannot verify against a known ROM CRC.
python3 tools/retrode3-readtest.py --system nes --action read-rom \
    --mapper 0 --prg-kib 32 --chr-kib 8 --mirroring horizontal \
    --output /tmp/retrode3-nrom-manual.nes

# MegaDrive basic read-only header probe / dump (standard linear ROM mapping)
python3 tools/retrode3-readtest.py --system md --action probe
python3 tools/retrode3-readtest.py --system md --action read-rom \
    --output /tmp/retrode3-md-test.bin
\`\`\`

The executable prints **one JSON result** on stdout and JSON errors on stderr. Exit status 0 means the requested read completed, 2 indicates an error, 3 indicates an NES ROM CRC mismatch. In the latter case the data is saved under \`<output>.unverified\`, and is **not** claimed to be a good dump. Existing output files are never overwritten; incomplete files are not published.

**Current limits:** NES mapper 0 only (NROM 16/32 KiB PRG, 0/8 KiB CHR) and standard/linear MegaDrive ROMs with a valid SEGA header, or a caller-supplied even ROM size. No NES bank-switching mappers, no SRAM/EEPROM dumps, no SNES yet, no Sonic & Knuckles/32X/SVP/EEPROM special cases, no restore/flash/write paths. This CLI is a practical hardware test and does **not** implement the new OSCR cartridge algorithms yet.

The automatic NES recognition uses **Sanni's original NES cartridge database** \`nes.json\` and the same first/last 512-byte PRG CRC32 candidates as the new OSCR NES core; if zero or multiple matches occur, normal ROM reading stops instead of guessing. A manual NROM override is possible. The separate \`nes-mappers.json\` remains the source for future dropdown selection; it does not identify games.

Compare the resulting ROM against an independently trusted dump using \`sha256sum\`. On errors, keep the JSON output for diagnosis. If a test fails, do not keep cycling cartridge power or attempt save/flash writes.
