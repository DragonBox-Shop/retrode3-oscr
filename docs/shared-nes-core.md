# NES mapper-0 dynamic core for the shared Retrode3 CLI

**Branch:** `feature/shared-nes-core`, stacked draft PR #7 on the
shared-host foundation PR #5. **Experimental, not released.**

## Why NES differs from SNES and MegaDrive

Physical NES cartridges contain no `.nes` or iNES header, so probing
the bus cannot determine ROM size, PRG/CHR geometry or mapper safely.
The pinned original Sanni NES core identifies cartridges by CRC32 of the
first and last 512 bytes of the CPU PRG area (addresses `0x8000` and
`0xE000`) against the NES database.

We preserve that approach and add a **full PRG+CHR CRC32 check before
allowing any read**. The frozen mapper-zero subset contains 226 candidate
records derived from the **pinned** original
`oscartreader/oscr-databases` commit
`87a48948c6a04260205f148d313614cc44e05cd5`
(`data/nes.json`); this source is referenced by the pinned OSCR firmware
submodule. Each record retains Sanni's two candidate fingerprints,
full PRG+CHR CRC, cartridge title, PRG and CHR sizes, and the exact
original 16-byte iNES / NES 2.0 header. The catalog is compiled offline
into `nes.so` and requires no network connection or runtime database.
The synthetic mock record is included only with `RETRODE3_TESTING`.

Only an **exactly unique full-ROM CRC match** is accepted. Unknown,
unsupported, ambiguous, all-zero or all-FF cartridges fail without
publishing a ROM. The initial implementation is deliberately read-only,
supports only mapper 0 and submapper 0, 16 or 32 KiB PRG and
0 or 8 KiB CHR, and excludes trainers.

## Upstream algorithms and Linux hardware bridge

The generator `tools/build-linux-nes-core.py` checks all four
original pinned C++ methods for exact code structure:
`NES::dumpPRG`, `NES::dumpCHR`, `NES::dumpBankPRG`,
`NES::dumpBankCHR`. A changed algorithm stops compilation pending review.

The **original `dumpBankPRG` / `dumpBankCHR` loops remain verbatim**.
Their inner `dumpPRG` / `dumpCHR` per-byte AVR reads are deliberately
replaced with 512-byte `ReadBus::nes_block` kernel read-only calls,
so the Linux kernel manages signal timing instead of tens of thousands
of userspace pin toggles. The shared POSIX storage shim forwards those
512-byte buffers to the core-operation ABI host callback. The new
`oscr_nes_adapter.cpp` contains no NDJSON parser, file output or CRC
progress logic. This all remains in the **single shared CLI**.

ROM output order: the exact cataloged 16-byte header, then original
Sanni PRG bank order, then CHR bank order. The `result.crc32` emitted
by the shared host is for **the entire .nes file including the header**.
The NES probe `checksum` is the OSCR database **raw PRG+CHR CRC**
*excluding* the header. These are two intentionally different CRCs.
The common host's `verified:null` remains correct because global
database verification is not yet integrated in the host; internal
NES layout selection is based on a pinned catalog fingerprint match.

No `nes_write()`, mapper bank controls, SRAM/EEPROM operations,
or save data writes are linked into the stage-1 core.
Enhancement mappers require separate reviewed safe kernel/HAL work,
especially when NES mapper operations could affect battery-backed RAM.

## Reproducible build and tests

```sh
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-native-nes-module
make -f Makefile.migration stage-nes-module
```

Both commands are **offline** after checkout. The first generates
the guarded original NES C++ bank loops and compiles the mock kernel
fixture, generic host, registry and `nes.so` adapter. Regression tests
cover complete `.nes` byte output, source guard, CRC32 distinction,
actual-byte progress, disabled module refusal, unsupported cartridge
rejection, cancellation and partial-output cleanup, no overwrites,
and SNES/NES coexistence via the same host.

The second creates the production `build/migration/nes.so` and copies
`nes.so` plus `packaging/cores/nes.core` to:
`build/migration/stage/usr/lib/retrode3/cores-available/`.
This does **not** install or enable a module on the live Retrode3.

## Live Retrode3 2.9.5 full NROM ROM read: PASSED (2026-11-19)

**Hardware acceptance achieved** on the actual MIPS Retrode3
2.9.5 with the installed, enabled original-Sanni-derived `nes.so`.
The shared CLI reports **md, nes and snes** `ready`.

Physical probe of genuine **Excitebike (Europe)** correctly returned
`mapping:nrom`, `title:Excitebike (Europe)`, `size_bytes:24592`,
`checksum:8A0C7337` (raw 24576-byte PRG+CHR).

The **complete physical ROM dump** was then executed through the same
shared host:

```sh
build/migration/retrode3-cli --protocol ndjson-v1 --core nes --action read-rom --output /tmp/excitebike-nes-plugin.nes
```

Central CLI (not plugin) produced NDJSON `progress` events at 0,
4112, 8208, 12304, 16400, 20496 and 24592 bytes, ending in:

```json
{"protocol":"retrode3-ndjson","version":1,"event":"result","request_id":"cli","ok":true,"system":"nes","bytes_written":24592,"crc32":"F749E780","output":"/tmp/excitebike-nes-plugin.nes","verified":null}
```

The original OSCR catalog supplies the 16-byte header
`4E45531A010101080000000001000020`. Its isolated CRC32 is
`43EABD6C`, and the payload database CRC32 is `8A0C7337`.
Independently combining the two CRC32s over the known 16 + 24576
bytes with `zlib.crc32_combine` gives **`F749E780`**, exactly
matching the read output. This validates the entire header+PRG+CHR
stream, shared host progress/output, and read-only bank-loop path.
No independent archival file `cmp` was submitted.

The host's `verified:null` and `candidate_lookup:not_integrated`
refer only to host-wide generic database integration: original OSCR
NES NROM catalog matching **is functioning** in the core.

This is standard **mapper-0 NROM only**. Special mappers and save
operations are not implemented; release packaging and a restricted
web enable/disable privileged bridge are separate work.

## Reproduction checklist for the now-passed physical stage-1 test

1. The independent MegaDrive stage-1 hardware acceptance has completed.
   Use separate worktrees for concurrent development to avoid overwriting
   uncommitted changes.
2. The production module built successfully on real Retrode3 2.9.5
   (GCC 4.9, C++11, Python 3.4). Rerun after any future code changes.
3. With an authentic **Excitebike (Europe)** cartridge in the NES slot
   and no legacy tool using the bus, controlled-install the packaged
   module (and matching descriptor), then activate with
   `retrode3-corectl enable nes`. Do not overwrite existing modules
   or auto-install from test targets.
4. Confirm `retrode3-cli --list-cores` marks `nes` installed,
   enabled and ready, while existing `snes` still reports ready.
5. Run *probe only* first:
   ```sh
   build/migration/retrode3-cli --protocol ndjson-v1 --core nes --action probe
   ```
   Expected: `mapping:nrom`, `title:Excitebike (Europe)`,
   `checksum:8A0C7337` (PRG+CHR CRC) and
   `size_bytes:24592` (16-byte NES header + 16 KiB PRG + 8 KiB CHR).
   Actual NES RAM contents are never written.
6. If the probe matches, read a new ROM filename:
   ```sh
   build/migration/retrode3-cli --protocol ndjson-v1 --core nes --action read-rom --output /tmp/excitebike-nes-plugin.nes
   ```
   Then compare with an independently verified .nes reference file
   **byte-for-byte** where an independent reference is available.
   The physically tested output is 24592 bytes and full-file CRC32
   `F749E780`, which is precisely the expected concatenation CRC
   from the original 16-byte header and payload CRC `8A0C7337`.

Do not claim MIPS hardware validation of this new `nes.so` until
the physical proof above has succeeded. Physical NES 2.9.5 timing and
edge cases may still need careful review.
