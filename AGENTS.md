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

Prefer static `.a` archives initially. Future `.so` or process plugins are
possible but not implemented or required today. A core compiled into a
library is not automatically a supported production feature.

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
