# Native MegaDrive C++ stage 1: safe linear read-only validation

**Status (2026-11-18):** The generated static OSCR MegaDrive C++ archive,
native Linux CLI and mocked ROM tests passed dedicated GitHub Actions
([native-md workflow run 44216393549](https://github.com/DragonBox-Shop/retrode3-oscr/actions/runs/44216393549)).
Physical hardware and byte-for-byte cartridge comparison are **pending**.
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

## First physical test (standard Sonic the Hedgehog 2, no special mapper)

Before inserting or reading the cartridge, keep the already hardware-proven
kernel and slot power behavior. Use one ordinary known cartridge and no
additional adapter. Confirm a matching original ROM reference exists.

```sh
build/migration/oscr-md-native --action probe

# Only after the probe reports a plausible standard linear ROM:
build/migration/oscr-md-native --action read-rom \
  --output /tmp/sonic2-oscr-md-cpp.bin

# Compare against the existing independent known-good ROM, if present:
cmp /tmp/sonic2-reference.bin /tmp/sonic2-oscr-md-cpp.bin
crc32 /tmp/sonic2-oscr-md-cpp.bin 2>/dev/null || true
sha256sum /tmp/sonic2-reference.bin /tmp/sonic2-oscr-md-cpp.bin
```

The `cmp` result (exit 0), size and SHA256 are more important than the
individual internal MD checksum. The binary also emits full-ROM CRC32, but
`verified:null` until compared to an independent reference. Run a second
dump to a **different** filename to test repeatability. Existing output files
are never overwritten, and partially written files are not published.

Do not probe or read any Sonic 3 FRAM, unusual mapper, lock-on, or special
cartridge yet. Subsequent phases will integrate upstream `getCartInfo()`,
the full read loop/checksum algorithm, safe mapper operations and the common
NDJSON transport only after separate bus safety review. NES follows MD.
