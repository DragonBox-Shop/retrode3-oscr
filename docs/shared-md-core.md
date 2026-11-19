# MegaDrive stage-1 dynamic OSCR core in the common Retrode3 CLI

**Branch:** `feature/shared-md-core`. **Draft PR:** #6, stacked on
`feature/shared-core-host` PR #5. No master merge or live OS install.

## Scope and trust boundary

- The generated, pinned original Sanni OSCR `MegaDrive::readWord()`
  and `copyToRomName()` remain in `oscr_md_core.cpp`; the generator
  rejects upstream GPIO/timing changes and never edits the submodule.
- The **same** C++11 implementation `oscr_md_stage1.cpp` provides
  the header probe and safe ROM-block streaming to the legacy hardware
  acceptance CLI and dynamically loaded `md.so` adapter.
- Only standard direct-mapped MegaDrive ROMs, 512 bytes to **4 MiB**.
  Rejected: 32X, SVP, Sonic & Knuckles lock-on, unknown header,
  invalid/oversize ROMs, mapper writes. Some atypical cartridges may
  require future explicit detection before they are safe to dump.
- Original `readWord()` uses the hardware-proven `MD_MODE_P10` HAL
  to read the standard header; ROM data uses the validated
  **read-only** `MD_MODE_ROM` kernel block API (2048-byte chunks).
  The adapter probes the identity a second time before streaming.
- The central CLI, **not the cartridge adapter**, owns NDJSON protocol,
  bytes-written progress, CRC32, output file publication, global bus
  locking, cancellation, error events and cleanup.
- `md.so` exports only `probe`, `read_rom` and `release`.
  It does not support SRAM/FRAM, EEPROM, flash, mapper writes,
  save restore, ROM writing or the complete upstream OSCR
  `getCartInfo()` / `readROM()` algorithms.

## Reproducible build and test

```sh
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-native-md-module
make -f Makefile.migration stage-md-module
```

This compiles the genuine pinned OSCR C++ functions, the real generic
CLI, the metadata registry and test-only mock kernel fixture. It
compares the **actual dynamic `md.so`** over a fake kernel bus
byte-for-byte with the formerly hardware-proven standalone MD reader.
It also checks the disabled-module gate, valid probe metadata,
unsafe cartridge rejection, cancellation, no-clobber files,
NDJSON host progress and host-generated CRC32.

Staging output (not installed, not enabled):

```text
build/migration/stage/usr/lib/retrode3/cores-available/md.so
build/migration/stage/usr/lib/retrode3/cores-available/md.core
```

Only a controlled, trusted root install under
`/usr/lib/retrode3/cores-available/` and a symlink in
`/etc/retrode3/cores-enabled/` can activate it. Never overwrite an
existing deployed module without an upgrade/rollback plan.

## Acceptance required on real Retrode3 2.9.5 MIPS

1. Verify `test-native-md-module` and `stage-md-module` on the MIPS
   device using its GCC 4.9/C++11 and Python 3.4 toolchain.
2. Only after tests pass, explicitly install this stage-1 module with
   correct ownership/permissions, enable via `retrode3-corectl`,
   check `retrode3-cli --list-cores` reports `md` ready alongside
   the previously installed SNES core.
3. With known standard cartridge **Snake Rattle n Roll (Europe)**
   present and no legacy reader running, issue only:
   ```sh
   build/migration/retrode3-cli --protocol ndjson-v1 --core md --action probe
   ```
   Expected original physical standalone header: size `524288`,
   checksum `2152`, serial `GM MK-1541 -50`,
   mapping `linear`. Generic probe ABI currently reports size,
   title, checksum and mapping, not serial.
4. If expected, read to a fresh output path:
   ```sh
   build/migration/retrode3-cli --protocol ndjson-v1 --core md --action read-rom --output /tmp/snake-rattle-n-roll-md-plugin.bin
   ```
   Expected 524288 bytes, full-ROM CRC32 `543BED30`.
   If the earlier standalone reference remains, `cmp` the outputs.
5. Log actual physical results and chip/mapper limitations separately.
   **Do not claim physical acceptance before the new shared-host
   `md.so` passes these checks.** 

The global cartridge bus lock protects only the new host. Legacy SNES,
MD, NES test readers are not lock participants: never run them at the
same time. The shipping OS still needs boot-time lock-directory
creation and output-root policies before general deployment.
