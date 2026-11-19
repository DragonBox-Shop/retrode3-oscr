# First genuine OSCR C++ execution stage on Retrode3 Linux

**State:** Experimental, read-only, build and mocked LoROM/HiROM tests in CI.
A **physical-cartridge test of the new C++ binary is still pending**.
The complete rewritten OSCR C++ SNES core is **not yet ported**.

## Why this stage exists

The new OSCR source in `third_party/oscr-firmware/src/cores/SNES.cpp`
still contains many AVR-only register operations plus Arduino menus, storage,
power, and special-chip paths. Merely setting a Linux architecture macro
cannot compile or safely execute the entire core.

Instead, `tools/build-linux-snes-core.py` uses the **pinned original source**
as a build input. It extracts the actual upstream C++ functions
`OSCR::Cores::SNES::checkcart`, `readLoRomBanks` and `readHiRomBanks`.
The original header parsing, checksum/mapping/size calculations, banking
loops and file-write order are retained. The generator substitutes only
the AVR GPIO loops and bank PORT assignments with `OSCR::RetroDe3::readBlock`
and initializes the upstream fallback 5-byte title buffer for safe POSIX use. This calls the existing typed
`retrode3::ReadBus`, which reads the existing kernel device in 512-byte
chunks in mode 0. Unexpected changes in upstream register reads are treated
as **hard build errors**, never guessed or fuzzy-patched.

No file in the upstream submodule is modified. Generated C++ lives only in
`build/migration/oscr_snes_banks.cpp`. It is compiled separately into
`build/migration/liboscr-snes-core.a`, which the Retrode3 CLI links against.
The OSCR source is therefore **not tied to an upstream executable**.
The archive currently contains only the three upstream-derived SNES functions,
not the whole upstream firmware or its special-chip and SRAM handlers.

This provides a **real executable implementation of the original upstream
C++ header-detection and bank algorithms** rather than another independent
Python rewrite. A limited POSIX compatibility layer supplies the required
OSCR global state, checksum, storage and formatting helpers.
It is deliberately only the first vertical slice of the intended native port.

## Build and native mocked-bus tests

From `migration/oscr-firmware`:

```sh
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-native-snes
make -f Makefile.migration native-snes
```

The native stage uses C++11 to keep the extracted bank functions compilable
with older Linux C++ compilers, including Debian 8's GCC 4.9. The **full
upstream firmware still requires a separate C++20-compatible port/build**.
The native test binary is `build/migration/oscr-snes-native`; the separate
linkable core library is `build/migration/liboscr-snes-core.a`. Future NES,
MegaDrive and other cartridge cores should follow the same arrangement.
This is static linking (not a runtime-loadable `.so` plugin) and keeps the
existing Retrode3 CLI, web UI and kernel interface free to evolve independently.

`test-native-snes` compiles the actual upstream-derived C++ functions and
exercises their bank selection and output using a synthetic kernel device
ABI, including a 4 MiB LoROM, a 4 MiB HiROM, smaller ROMs, and protection
against overwriting files. This test does not prove physical bus timing.

## First physical cross-check, read-only

**Use only known standard cartridges already identified by the Python
OSCR read-test probe**, not unknown special-chip carts.

The original upstream C++ header check is now runnable directly:

```sh
build/migration/oscr-snes-native --action probe
```

For the previously verified Super Turrican (Europe):

```sh
# Build as above on the Retrode3 if its C/C++ toolchain is available,
# or cross-compile for its MIPS architecture separately.
build/migration/oscr-snes-native \
  --mapping lo --size-bytes 524288 --output /tmp/superturrican-oscr-cpp.sfc

cmp /tmp/super-turrican-europe.sfc /tmp/superturrican-oscr-cpp.sfc \
  && echo 'Original OSCR C++ LoROM bank reader is bit-identical!'
```

For the previously verified Terranigma (Germany) (Rev 1):

```sh
build/migration/oscr-snes-native \
  --mapping hi --size-bytes 4194304 --output /tmp/terranigma-oscr-cpp.sfc

cmp /tmp/terranigma-germany-rev1.sfc /tmp/terranigma-oscr-cpp.sfc \
  && echo 'Original OSCR C++ HiROM bank reader is bit-identical!'
```

The native CLI now runs the **original upstream OSCR checkcart()** for
header/mapping/size recognition; use `--action probe` without a mapping or
size override to inspect it first. `--action read-rom --output FILE` can then
use those detected values automatically. The **original OSCR CRDB matching
and alternate-size correction are not yet linked**, so only ordinary
power-of-two LoROM/HiROM cartridges of known supported chip types are safe.
The native reader performs **no write operations to cartridges**.
Output files are staged in the destination directory and published without
clobbering existing files. For a failure, no finished ROM is published.

## What remains before replacing the old engine

1. Wire the original upstream C++ SNES CRDB and alternative size/mapper
   configuration to the now-compiled original checkcart(); only the standard
   header path is currently available.
2. Replace remaining AVR bus operations with typed Linux HAL hooks, including
   required special-chip and save-RAM operations and clock/power lifecycle.
3. Implement reusable POSIX Storage, UI, Time, Power, and architecture
   interfaces without manual source editing in the submodule.
4. Add the corresponding NES and MegaDrive C++ core build and hardware
   dispatch, keeping actual cartridge mapper algorithms from upstream.
5. Integrate the noninteractive CLI and web UI, test hardware readback and
   CRCs, then consider replacing the legacy Linux CLI in production.

Do not claim this stage is a complete native OSCR firmware port.
