# Retrode3 OSCR development instructions

This file is for human contributors and coding assistants. For current progress,
next steps, and verified hardware evidence, read
[docs/PROJECT-STATUS.md](docs/PROJECT-STATUS.md) **before changing code**.
Update it when a milestone, a blocker, or a verification result changes.
Do not rely on previous chat history to determine the current project state.

## The product goal

Retrode3 is a Linux-based cartridge reader. Reuse **Sanni's original OSCR
cartridge readers, mappers, and special-chip algorithms** in libraries called
by **our own** Retrode3 CLI/web UI. We do **not** need to execute the complete
original OSCR firmware binary or reproduce its menus. Support Retrode2-style
plugin adapters and additional Retrode3 hardware using the same interfaces.

The target is:
Retrode3 CLI/web UI -> OSCR (and custom) cartridge core libraries ->
typed Linux bus/HAL -> Retrode3 kernel driver -> cartridge.

Static `.a` archives remain known-good hardware acceptance references.
On `feature/shared-core-host`, a real SNES `.so` adapter is generated
from the original pinned C++ read routines and loaded via a versioned
Retrode3 C operation ABI. Its standard HiROM path was **physically verified**
with Terranigma (Germany) Rev 1 (4 MiB, CRC32 `6FB8A4DF`).
On `feature/shared-md-core`, the original-derived MegaDrive stage-1
reader likewise exposes `md.so` and passes CI/mock byte-for-byte
comparisons, but its new dynamic path is **not yet physically verified**.
Neither stage-1 adapter is released as a shipping OS package.
Dynamic adapters reuse the same generated upstream C++ functions and
shared typed Linux HAL, not a fork of Sanni's algorithms.
A core compiled into a library is not automatically a supported production
feature.

## Repositories

- `DragonBox-Shop/retrode3-oscr`: this repo, Linux cartridge CLI,
  Retrode3 libraries and pinned OSCR firmware source.
- `DragonBox-Shop/retrode3-kernel`: Linux kernel, slot driver, electrical
  GPIO control and timing. Do not emulate nanosecond-sensitive pulses by
  dozens of independent userspace operations.
- `DragonBox-Shop/retrode3`: Debian-based Retrode3 OS and management UI.
- `DragonBox-Shop/retrode3-hardware`: boards, adapters and schematics.
- Upstream: `oscartreader/oscr-firmware` (the redesigned Sanni OSCR tree).
  `sanni/cartreader` is legacy, kept for comparison.

## Branch and dependency rules

- Active integration branch: `migration/oscr-firmware`. **Do not merge to
  `master` or release by default.**
- `third_party/oscr-firmware` is a **pinned, unmodified Git submodule**.
  Never hand-edit files in this checkout. Place small, reproducible,
  version-controlled compatibility overlays in our own tree.
- Unrecognized upstream changes to hardware-sensitive source MUST stop the
  build and require a deliberate review. Do not silently apply fuzzy
  replacements.
- Keep generic bug fixes and reusable HAL hooks suitable for upstream PRs.
  Do not push to upstream without the owner's permission.
- Retain the existing `/dev/slot-{nes,md,snes}` ABI unless a separately
  reviewed kernel change is essential.

## Shared host runtime versus cartridge cores (decision 2026-11-18)

**One Retrode3-owned native CLI/runtime owns the protocol for all systems.**
Do NOT copy the SNES NDJSON command parser, progress/event formatter,
cancellation loop, output publication, CRC verification, job IDs or
web-facing behavior into the MegaDrive, NES or other cartridge cores.
The existing `oscr-snes-native` and `oscr-md-native` executables are
**temporary hardware-acceptance runners**, not a precedent for duplicated
per-core production CLIs. `ndjson_protocol.cpp` is a reusable host building
block; move SNES-specific command dispatch into an adapter when extracting
and registering cores in one CLI.

Core boundary: register per-core identification, safe capability descriptor,
probe, ROM/save read and permitted control operations. Core implementations
should contain cartridge algorithms, not JSON or HTTP dependencies. The
common host owns: NDJSON v1 stdin/stdout protocol, job lifecycle, polling
and cancellation, actual-bytes-written progress, atomic no-clobber output,
checksums/database result reporting, error codes, exclusive cartridge-bus
access, and validated paths/options. Host POSIX implementations of upstream
OSCR Storage/UI/Power/Time abstractions bridge the original Sanni callbacks
to these services. Keep the pristine upstream submodule unchanged; small
reviewable generated HAL substitutions may still be necessary for direct AVR
GPIO access, but **never add per-console NDJSON or UI code upstream**.

Capability reporting distinguishes physical hardware support, recognized
cartridge type, and currently implemented/tested operations. A read-only
user action may require controlled mapper-register writes; gate these via
explicit reviewed HAL functions and do not expose unsupported save/mapper
operations just because they exist in upstream C++. The web backend remains
its own service in the OS repo and communicates with this common CLI; do
not modify its implementation in this repository as part of core porting.

## Installable core discovery (decision 2026-11-18)

**Ship all released and hardware-safe software cores preinstalled on
the Retrode3 SD-card OS image**, so normal use is fully offline. Keep
independently installable/updateable `.so` packages, not compiled into the
main CLI: package-managed modules and static `.core` metadata under
`/usr/lib/retrode3/cores-available`, with Apache-style **root-owned symlinks**
in `/etc/retrode3/cores-enabled` pointing only to the exact corresponding
module. Disabled modules remain listed but are never loaded; enabling or
disabling must work offline without apt or downloading anything. The
`retrode3-corectl` root-only fixed-path helper performs the symlink toggle,
not the unprivileged discovery CLI or a browser with general root access.
The initial static `.a` acceptance libraries are not installable modules.
`retrode3-cli` emits the `cores` NDJSON event with separate `installed`,
`enabled`, `status`, `usable` and `actions` fields. See
[docs/core-registry.md](docs/core-registry.md). **Discovered != usable**:
never show installed but unimplemented cores as callable operations.
Distinguish software cores, physically inserted cartridge adapters, slot
compatibility and cartridge presence; all are separate states. Reject
user-supplied plugin paths and untrusted/misdirected symlinks. The operation
dispatch ABI, SD-image packaging and physical verification follow separately.

## Shared runtime implementation boundary (2026-11-18)

The new `feature/shared-core-host` branch implements a shared NDJSON
multi-core runner, operation ABI v1 and a genuine (still stage-1 limited)
SNES dynamic adapter. All bytes-written progress, cancellation, CRC32,
atomic no-overwrite file publication and command validation stay in the
host. The pinned original `SNES.cpp` bank-read code is generated unchanged
except for strictly guarded AVR-to-HAL substitutions; the existing legacy
SNES acceptance binary reuses the same POSIX shim. See
[docs/shared-core-host.md](docs/shared-core-host.md).

The new CLI uses a nonblocking `flock` on
`/run/lock/retrode3/bus.lock` per physical job; the OS image must securely
create `/run/lock/retrode3` and grant the service account appropriate
access. **Legacy acceptance binaries do not yet participate**, so this is
not a guarantee of exclusivity while those binaries run independently.
Keep the original SNES/MD hardware paths available for regression checks.

## MegaDrive real dynamic adapter (draft, 2026-11-19)

The stacked `feature/shared-md-core` draft PR #6 builds
`build/migration/md.so` using the pinned original Sanni MD
`readWord()`/naming-helper code and `src/retrode3/oscr_md_stage1.cpp`.
That stage-1 helper is shared by the prior standalone hardware acceptance
CLI and the new dynamic adapter. It supports standard linear read-only
MegaDrive ROMs up to 4 MiB, no mapper control, special carts or SRAM.
The host, not the module, handles progress, CRC32, NDJSON, cancellation,
output files and bus locking. CI compares the real module's bytes
with the prior standalone mock reader and exercises cross-core dispatch.
The dynamic MegaDrive .so has now passed a real physical full-ROM dump
(Snake Rattle n Roll, CRC32 `543BED30`).
See [docs/shared-md-core.md](docs/shared-md-core.md).

## Parallel NES mapper-0 dynamic core (2026-11-19)

The independent `feature/shared-nes-core` branch (draft PR #7,
stacked on PR #5 but **not** on in-progress MD PR #6) adds a
genuine original Sanni NES bank-loop adapter `nes.so` for the same
shared CLI. A strict original-source guard checks `dumpPRG/dumpCHR`
and `dumpBankPRG/dumpBankCHR`; **Sanni's exact PRG/CHR bank loops
remain unchanged** and the AVR per-byte reads are replaced with safe
512-byte Linux `ReadBus::nes_block` transactions. Do not copy NDJSON,
progress, output or CRC into this module.

**NES carts have no header.** Probe only explicitly supported mapper-0
NROM via pinned OSCR first/last 512-byte fingerprint lookup followed
by a complete raw PRG+CHR CRC check against the frozen 226-record
NES NROM catalog. If missing or ambiguous, **refuse to dump**, never
guess mapper/geometry. Output begins with exact trusted 16-byte header
from the matched catalog; host `result.crc32` includes header, but
the NES probe `checksum` is raw PRG+CHR CRC excluding header.
No NES mapper, SRAM, save or flash writes in this stage.

Real MIPS NROM full-ROM acceptance is complete: Excitebike Europe,
24592 bytes, full-file CRC32 `F749E780`. See [docs/shared-nes-core.md](docs/shared-nes-core.md).
NES higher mapper support requires separately reviewed controlled
kernel writes and safety gates.

## Safe engineering workflow

1. Read `docs/PROJECT-STATUS.md` and relevant per-feature docs.
2. Verify the current Git branch, clean/dirty state, and pin versions.
3. Make the smallest change with an explicit test; no unrelated refactors.
4. Run CI, then have the user run **read-only** acceptance tests on real
   hardware. Record concrete ROM size, mapping, CRC32 and repeatability.
5. Track `implemented`, `CI/mock tested`, and `hardware verified` as
   distinct claims. Never claim a complete C++ OSCR core is ported just
   because a few original C++ functions compile.
6. Before switching chats, update the project status with the working build,
   known failures, next task, and safe reproduction commands.

Never perform arbitrary writes to save RAM, flash/mapper registers, or
cartridge storage during generic ROM probing. Any write operation needs a
separately designed safety and approval path. Do not overwrite existing ROM
outputs. Protect carts against incorrect voltage or timing assumptions.

## Hardware and toolchain reality

- Retrode3 2.9.5 test device: old Debian / Letux MIPS with Python **3.4**,
  so use `io.open` and `os.makedirs` instead of newer `pathlib` helpers.
- The native SNES **partial** C++ stage is compiled as **C++11** to support
  the device's older GCC; original full OSCR firmware is C++20-oriented.
- The native library stage is `make -f Makefile.migration native-snes`,
  producing `build/migration/liboscr-snes-core.a` and
  `build/migration/oscr-snes-native`.
- Test with `make -f Makefile.migration test-native-snes`.
  The CI workflow initializes `third_party/oscr-firmware` before building.
- The embedded device may lack compilers, or hit legacy C compatibility
  issues. Debug build errors in Git, commit the fix, validate CI, then retry
  on hardware.

## Start of a new session

Suggested first instruction to any assistant:

> Read AGENTS.md, docs/PROJECT-STATUS.md, and the active migration docs
> in DragonBox-Shop/retrode3-oscr on migration/oscr-firmware. Also inspect
> relevant current GitHub state. Continue the first pending task, without
> reimplementing existing OSCR cartridge algorithms or changing master.

This file is a source of truth **when actually read**; ChatGPT does not
necessarily load a repository's AGENTS.md automatically in every chat.
