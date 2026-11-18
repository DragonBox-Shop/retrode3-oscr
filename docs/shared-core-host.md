# Shared Retrode3 core host: dynamic operation API and SNES adapter

**State:** experimental, branch `feature/shared-core-host`, draft PR #5.
This is the foundation of the common runtime, not a production replacement
for the existing hardware-tested SNES/MD acceptance readers.

## Architecture

```text
Web backend (separate OS repository; not yet connected)
  -> ONE retrode3-cli [NDJSON v1, job lifecycle, progress/cancel/CRC/output]
    -> fixed-path, root-installed core discovery and enablement registry
      -> versioned C core-operation ABI in a .so Retrode3 adapter
        -> generated original Sanni OSCR C++ functions
          -> POSIX OSCR shim -> ReadBus HAL -> Retrode3 kernel
```

The pinned original OSCR submodule remains untouched. The build generator
checks the original source and fails on unreviewed direct AVR changes.
Only Retrode3 code has a JSON protocol, input parser, file output, CRC and
progress counter. The new `include/retrode3/core_operations_v1.h`
provides a C-compatible ABI with `probe`, `read_rom`, `release`, and
host `write` / `cancelled` callbacks. Core modules must never allow
C++ exceptions across this boundary. The host accepts a byte stream and
owns no-overwrite output publication and CRC32 computation. Progress reflects
**successful file-write bytes**, not guessed bus reads.

## Build and mocked tests

```sh
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration native-core-registry
make -f Makefile.migration test-core-registry
make -f Makefile.migration native-snes-module
make -f Makefile.migration test-native-snes-module
make -f Makefile.migration stage-snes-module
```

`build/migration/retrode3-cli` is the real central host. The test-only
`retrode3-cli-mock` permits custom fixture paths. The real CLI accepts no
module/manifest directory overrides. Installed modules live under
`/usr/lib/retrode3/cores-available` and are enabled with trusted symlinks
under `/etc/retrode3/cores-enabled`. A disabled core is listed without
`dlopen`; a descriptor-only module is not runnable. Enabled modules are
loaded to inspect their metadata and operation ABI: **shared-library
constructors, descriptor functions and operation-getter functions must never
access a cartridge or toggle its power**. Actual cartridge operations run
only after acquiring the common-host bus lock.

The core-registry CI uses mock modules and the real **generated upstream
SNES C++ code with a fake kernel** to compare byte-for-byte against the
existing native SNES acceptance runner. Cancelling a read must remove
temporary output and produce no successful result. Re-running the
older SNES/MD acceptance suites protects the already-proven physical paths.

## Protocol

List installed and disabled modules without loading cartridge hardware:

```sh
build/migration/retrode3-cli --list-cores --protocol ndjson-v1
```

For installed and enabled cores only, the same host offers both interactive
and one-shot NDJSON commands:

```sh
build/migration/retrode3-cli --protocol ndjson-v1 --core snes --action probe
build/migration/retrode3-cli --protocol ndjson-v1 --core snes --action read-rom --output /tmp/known-good.sfc
```

Interactive mode:

```sh
build/migration/retrode3-cli --protocol ndjson-v1 --interactive
```

One JSON command per line:

```json
{"command":"list_cores","request_id":"list1"}
{"command":"get_capabilities","core":"snes","request_id":"caps1"}
{"command":"probe","core":"snes","request_id":"probe1"}
{"command":"read_rom","core":"snes","request_id":"rom1","output":"/tmp/known-good.sfc"}
{"command":"cancel","request_id":"rom1"}
```

Each command is a **separate** NDJSON line, not an array. The `cancel`
command may arrive while `read_rom` runs; its request ID must match the
running job. The host sends `ready`, `cores`, `capabilities`,
`probe_result`, `progress`, `result`, and `error` events with the
common protocol/version prefix where supported. Progress ends at 100%
only after successful no-clobber file publication; `verified:null` remains
until original OSCR CRDB verification is implemented.

The host currently refuses mapping/size overrides that differ from the
probe result. Validated backend-controlled absolute output paths are
required: the CLI rejects relative/traversal paths but **does not enforce
a configured write-root**. The web backend must enforce its own private
output directory and never pass arbitrary browser-supplied paths to CLI.
Cartridge operations in the **new shared CLI** use an exclusive,
nonblocking Linux `flock` at `/run/lock/retrode3/bus.lock`, held through
core release. The OS must securely create `/run/lock/retrode3` with
permissions allowing the CLI service account to create/access its lock.
The **old standalone acceptance binaries do not use this lock**. Until they
are retired or updated, do not run them concurrently with the shared CLI;
additional web-backend orchestration is still needed.
Generic `--core ID --action probe|read-rom` is also supported in NDJSON
one-shot mode, and invokes the same operation host. Unlike interactive
mode, one-shot jobs cannot be cancelled via stdin while running.

## First actual dynamically loadable original OSCR core

`make -f Makefile.migration native-snes-module` produces
`build/migration/snes.so` linked with the **real original pinned**
`OSCR::Cores::SNES::checkcart()`, `readLoRomBanks()` and
`readHiRomBanks()` from our strict build generator. Its Retrode3 adapter
and shared POSIX shim live separately in
`src/retrode3/oscr_snes_adapter.cpp` and
`src/retrode3/oscr_snes_posix.cpp`.
The legacy `oscr-snes-native` runner now reuses that same POSIX shim.

Scope is **standard SNES LoROM/HiROM up to 4 MiB without special chips or
SRAM**. This does NOT expose enhanced-chips, SRAM, save restoration, or a
complete original OSCR SNES ROM dispatcher. The same ABI can later wrap
the original MegaDrive, NES and their mapper/save operations.

An exact-version `packaging/cores/snes.core` is included, and
`stage-snes-module` copies both files into a **non-live**
`build/migration/stage/usr/lib/retrode3/cores-available` tree. The files are
*not* automatically installed in the device's live filesystem by the build. Debian image packaging is a separate OS-repository task.
Do not install a module as if hardware-verified solely on successful CI.

## MIPS hardware acceptance (build and all 41 simulated tests passed; physical ROM pending)

The real Retrode3 2.9.5 successfully compiled and staged `snes.so`
with GCC C++11. `retrode3-cli --list-cores` returned an empty list as
expected before installation. The first target-side legacy SNES test suite
failed on 8 uses of Python 3.5+ `pathlib` file I/O methods, unrelated to
the core implementation. Those calls were replaced with Python
3.4-compatible `io.open()`, and the next target-device run **passed
all 16 legacy SNES tests**.

That run next failed when the MIPS GCC 4.9 compiler rejected the shared
CLI's `retrode3_probe_v1 result = {};` under
`-Werror=missing-field-initializers`. That initialization is now replaced
by explicit `std::memset` of the C ABI POD structure. A further real-target `test-native-snes-module` run **passed all
41/41 tests**: 1 Python 3.4 compatibility guard, 16 old SNES tests,
11 registry tests, 9 common-host tests and 4 genuine upstream-derived
SNES `.so` adapter tests over a fake kernel bus. The GCC 4.9 host build
succeeded. This does not yet constitute a physical cartridge read/CRC
test of the new dynamic host.


The build and CI run only virtual/mock cartridge hardware. The actual
Retrode3 2.9.5 MIPS native shared module has **not** been physically
verified. Begin with compilation and the safe, hardware-free test suite:

```sh
git fetch origin
git checkout feature/shared-core-host
git pull --ff-only
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-native-snes-module
make -f Makefile.migration stage-snes-module
build/migration/retrode3-cli --list-cores
```

Do not expect a usable core listing yet: build/stage never installs/activates
anything. Before manual testing, ensure the host/device already has a known
ordinary SNES cartridge (e.g. the previously verified Terranigma Germany
Rev 1) and the reference ROM/CRC. Do not test enhanced chips/unknown
cartridges. Stop if any of the previous build/tests fail.

For a *manual experimental install only*, root creates the fixed directories
including the bus lock location. Check for existing modules first: do not
overwrite any preinstalled production package or registered module.

```sh
install -d -m 755 /usr/lib/retrode3/cores-available
install -d -m 755 /etc/retrode3/cores-enabled
install -d -m 755 /run/lock/retrode3
# Only if the destination module and manifest do not already exist.
# Stop and inspect first if they are already installed by a package.
if [ ! -e /usr/lib/retrode3/cores-available/snes.so ] && \
   [ ! -e /usr/lib/retrode3/cores-available/snes.core ]; then
    install -m 644 build/migration/snes.so /usr/lib/retrode3/cores-available/snes.so
    install -m 644 packaging/cores/snes.core /usr/lib/retrode3/cores-available/snes.core
    build/migration/retrode3-corectl enable snes
else
    echo 'Existing SNES installation detected: inspect before any changes'
fi

build/migration/retrode3-cli --list-cores
build/migration/retrode3-cli --protocol ndjson-v1 --core snes --action probe
```

Only after a plausible probe, perform a read-only dump to a fresh output
path; compare CRC32 and `cmp` against the existing trusted Terranigma
reference if available. Never run the legacy SNES acceptance reader
**concurrently** with this new host: the legacy binary does not acquire the
shared bus lock. Reboot recreates `/run/lock/retrode3` only when the OS
image/service has provided an appropriate tmpfiles/init rule; this
manual directory is not a permanent packaging solution.

## Unfinished foundation work (in priority order)

1. Verify MIPS build, core listing, and safe physical read/CRC of the real
   SNES module against the already verified Terranigma reference before
   enabling a module in the shipping image.
2. Move per-console protocol-specific code out of legacy acceptances
   after the common host has parity; keep them as rollback/test references.
3. Extend the already implemented common-host `flock` protection to the
   remaining legacy access paths, then add backend-owned output roots and
   privilege separation, bounded resource policies, robust per-core error
   taxonomy and recovery on abnormal exit.
4. Build signed/offline packages and ship all **released/verified** cores
   on the SD image; secure privilege bridge for web toggles.
5. Extend the generic adapter ABI for `read_save`, mapper/capability
   policies and database matching. Then implement additional Sanni core
   functions with source-guarded GPIO substitutions and physical tests.

Do not change the pinned upstream firmware or merge draft work to master
without physical acceptance.
