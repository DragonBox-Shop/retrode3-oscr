# Native MegaDrive C++ stage 1: safe linear read-only validation

**Status (2026-11-18):** The generated static OSCR MegaDrive C++ archive,
native Linux CLI and mocked ROM tests passed dedicated GitHub Actions
([native-md workflow run 44216393549](https://github.com/DragonBox-Shop/retrode3-oscr/actions/runs/44216393549)).
A **physical read-only header probe and two bit-identical 512-KiB
ROM dumps are verified** on Retrode3 2.9.5. Full-ROM CRC32 `543BED30`
matches the pinned original OSCR `md.json` entry for
`Snake Rattle n Roll (Europe)`. The native CLI still reports
`verified:null` because the database lookup is not yet integrated.
Work remains isolated in draft PR #3 / `feature/native-md-stage1` against
`migration/oscr-firmware`. Do not merge into `master`.

The source generator extracts two methods from the pinned original
`third_party/oscr-firmware/src/cores/MegaDrive.cpp`:
- `readWord()`: same upstream entrypoint and word-address argument; its
  hardware register control and timing are replaced with one typed Linux
  `ReadBus::md_word()` operation using the existing kernel `MD_MODE_P10`.
- `copyToRomName()`: unchanged original C++ implementation.

The generator verifies both source bodies against exact expected semantics,
so upstream changes fail the build instead of being silently rewritten.
The resulting `liboscr-md-core.a` remains separate from the native CLI.
No files in the upstream submodule are changed.

The CLI probes a standard MD header through the original `readWord()`, then
uses the already tested `MD_MODE_ROM` byte-addressed bulk read via typed HAL
to dump an ordinary linear ROM. This bulk read is a Linux operation, **not**
the full original upstream `readROM()` function, whose embedded bank-switching,
save handling, and special-chip paths still need a dedicated safe port.
The partial CLI does not use the original `getCartInfo()` corrections.
Raw header size and filename are not automatically database-verified.

Unsupported / blocked: any ROM size over 4 MiB, 32X, SVP, Sonic & Knuckles
lock-on (identified by known header markers), mapper writes, SRAM/EEPROM/FRAM
reads and writes, special cartridges, 5V overrides and flash operations.
Header checks cannot recognize every nonstandard mapper; **use only a known
ordinary cartridge** for the first physical test.

## Build and test (Linux or real Retrode3)

```sh
git fetch origin
git checkout feature/native-md-stage1
git pull --ff-only
git submodule update --init third_party/oscr-firmware

make -f Makefile.migration test-native-md
make -f Makefile.migration native-md
```

## Completed first physical test (Snake Rattle n Roll, standard linear MegaDrive ROM)

The physical `--action probe` returned for **Snake Rattle n Roll (Europe)**:
`header_size_bytes: 524288`, `checksum: 2152`, `serial: GM MK-1541 -50`,
`region: E`, `mapping: linear`. Two full linear 524288-byte ROM reads
both produced CRC32 `543BED30` and `cmp` succeeded. SHA256 for the
first dump was
`26643484b639a10e0f889940a1556acbd8f9bcddb6de5b5a0ff2167104605200`.
The pinned original
[OSCR MD database](https://github.com/oscartreader/oscr-databases/blob/87a48948c6a04260205f148d313614cc44e05cd5/data/md.json)
has `Snake Rattle n Roll (Europe)`, CRC32 `543BED30`: **match**.
This validates the ordinary linear read-only path, not the full upstream
MegaDrive `getCartInfo()` / `readROM()` core or special mappers.

```sh
build/migration/oscr-md-native --action probe

# Only after the probe reports a plausible standard linear ROM:
build/migration/oscr-md-native --action read-rom \
  --output /tmp/snake-rattle-oscr-cpp.bin

# Second independent read: check physical repeatability.
build/migration/oscr-md-native --action read-rom \
  --output /tmp/snake-rattle-oscr-cpp-2.bin
cmp /tmp/snake-rattle-oscr-cpp.bin /tmp/snake-rattle-oscr-cpp-2.bin
sha256sum /tmp/snake-rattle-oscr-cpp.bin /tmp/snake-rattle-oscr-cpp-2.bin

# Only once a trusted Snake Rattle N Roll reference is available:
# cmp /path/to/verified-snake-rattle-reference.bin /tmp/snake-rattle-oscr-cpp.bin
```

The repeated full-ROM read, `cmp` result (exit 0), and matching pinned
OSCR database CRC32 are now **hardware-confirmed**. `verified:null` in
the output JSON remains accurate for the *binary's own capabilities*,
because database verification is not wired into this native stage.
Existing output files are never overwritten, and incomplete files are
not published.

Do not probe or read any Sonic 3 FRAM, unusual mapper, lock-on, or special
cartridge yet. Subsequent phases will integrate upstream `getCartInfo()`,
the full read loop/checksum algorithm, safe mapper operations and the common
NDJSON transport only after separate bus safety review. NES follows MD.
