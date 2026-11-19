# First Retrode3 read-only hardware test

**State:** Experimental hardware-acceptance CLI, **not** the new OSCR C++ core port. It is deliberately separate from the stable `oscr` program and exercises the same Linux `/dev/slot-nes` / `/dev/slot-md` ABI without changing `master`. Test with a known expendable cartridge first; no voltage/power setup or save writing has been added here. The existing kernel must already initialize the slots correctly.

Requirements: The read-test script has been adapted for **Debian 8 / Python 3.4** (physical-device validation is still outstanding). No `git switch`, new Python or Node.js is needed for this test. The original OSCR C++ firmware is not yet the executable. Clone before running commands:

```sh
mkdir -p /usr/local/src
cd /usr/local/src
git clone -b migration/oscr-firmware https://github.com/DragonBox-Shop/retrode3-oscr.git
cd retrode3-oscr
```

If already cloned earlier, from within the repository use `git fetch origin` followed by `git checkout migration/oscr-firmware` or `git pull --ff-only` on the migration branch. Debian 8 Git lacks `git switch`.

**Submodules and tests:**


```sh
# The nested OSCR databases submodule uses an SSH URL. Rewrite it temporarily
# if this host has HTTPS but no GitHub SSH credentials:
git submodule update --init third_party/oscr-firmware
git -C third_party/oscr-firmware \
    -c url.https://github.com/.insteadOf=git@github.com: \
    submodule update --init databases

python3 -m unittest discover -s tests -p 'test_readtest.py' -v

# NES cartridge identification, read-only (uses original OSCR data/nes.json)
python3 tools/retrode3-readtest.py --system nes --action probe

# NES mapper 0/NROM: dump with original iNES or NES 2.0 header, verify CRC32
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
# The JSON now includes domestic_title, overseas_title, serial and region
python3 tools/retrode3-readtest.py --system md --action read-rom \
    --output /tmp/retrode3-md-test.bin
```

The executable prints **one JSON result** on stdout and JSON errors on stderr. Exit status 0 means the requested read completed, 2 indicates an error, 3 indicates an NES ROM CRC mismatch. In the latter case the data is saved under `<output>.unverified`, and is **not** claimed to be a good dump. Existing output files are never overwritten; incomplete files are not published.

**Current limits:** NES mapper 0 only (NROM 16/32 KiB PRG, 0/8 KiB CHR, iNES 1.0 and simple NES 2.0 headers), standard/linear MegaDrive ROMs, and an **experimental read-only SNES LoROM/HiROM implementation** aligned with the new OSCR header/bank algorithms. See [SNES read-only hardware acceptance](snes-readonly.md); SNES still requires real-cartridge verification. No NES bank-switching mappers, no SRAM/EEPROM dumps, no SNES enhancement chips, no Sonic & Knuckles/32X/SVP/EEPROM special cases, no restore/flash/write paths. This CLI is a practical hardware-test adapter, **not** a native build of the new OSCR C++ cores.

The automatic NES recognition uses **Sanni's original NES cartridge database** `nes.json` and the same first/last 512-byte PRG CRC32 candidates as the new OSCR NES core; if zero or multiple matches occur, normal ROM reading stops instead of guessing. A manual NROM override is possible. The separate `nes-mappers.json` remains the source for future dropdown selection; it does not identify games.

For MegaDrive, if the original OSCR `md.json` database is present and its full-ROM CRC32 matches, the JSON result includes the game name and `verified:true`. Missing or unmatched MD records leave `verified:null` (not automatically corrupt). Compare the resulting ROM against an independently trusted dump using `sha256sum`. On errors, keep the JSON output for diagnosis. If a test fails, do not keep cycling cartridge power or attempt save/flash writes.

## Excitebike (Europe) live test

The OSCR cartridge database record for the real European Excitebike cartridge uses NES 2.0 header `4E45531A010101080000000001000020`; the header specifies mapper 0, 16 KiB PRG and 8 KiB CHR. The first test mistakenly rejected NES 2.0 outright. The corrected test supports these safe NROM sizes while still blocking nonzero NES 2.0 submappers and exponent-encoded sizes.

```sh
git pull --ff-only
python3 -m unittest discover -s tests -p 'test_readtest.py' -v
python3 tools/retrode3-readtest.py --system nes --action read-rom \
  --match-crc32 8A0C7337 --output /tmp/excitebike-europe.nes
```

A known-good original dump should report `verified: true`. This is a real cartridge verification still to be performed.
