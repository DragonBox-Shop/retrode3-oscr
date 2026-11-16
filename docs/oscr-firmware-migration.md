# Migration to the redesigned OSCR firmware

> Status: **experimental / scaffolding only**. The firmware in `third_party/oscr-firmware` is not yet compiled or executable on Retrode3. Do not deploy this branch to production hardware.

## Baselines

- Stable Retrode3 implementation: `master`, starting commit `3bbc215bfc1a972a53fb69716fb86ac94e07b941`.
- New upstream firmware: https://github.com/oscartreader/oscr-firmware
- Initial upstream revision: `e5a08326dfc8fff67193f7fc5a7bc36b5282b28c` (2026-11-11).
- Legacy upstream `sanni/cartreader` is archived; the original `Cart_Reader/`, `Makefile`, `oscr-cmd.*`, and `retrode3-lib.*` are retained for comparison.

## Non-negotiable goals

1. **Never edit the upstream cartridge cores in-tree.** All files underneath `third_party/oscr-firmware` are the unchanged upstream Git revision. Update upstream by changing the submodule pointer, not by hand-merging `src/cores/NES.cpp`, `src/cores/MegaDrive.cpp`, or `src/cores/SNES.cpp`.
2. Retain Linux kernel slot ABI and the existing Retrode3 device library (`/dev/slot-nes`, `/dev/slot-md`, `/dev/slot-snes`) unless a separately coordinated kernel change is necessary.
3. Keep the command-line program usable non-interactively from a web interface. Return reliable exit status, machine-readable progress/status, and output-file paths.
4. Preserve `master` until builds, integration tests, and real-cartridge verification pass.

## Architectural assessment (2026-11-16)

The new upstream has moved from a large collection of Arduino `.ino` files to PlatformIO, C++20, and namespaces under `OSCR::`. Major pieces include:

| Upstream | Retrode3 Linux integration needed |
| --- | --- |
| `src/cores/{NES,MegaDrive,SNES}.cpp` | Preserve unchanged; attach cartridge operations through suitable HAL |
| `include/arch.h`, `include/arch/syslibinc.h` | Introduce a Linux target/architecture path |
| `src/arch/avr/**`, `include/arch/avr/**` | Supply Linux equivalents rather than emulating AVR hardware |
| `OSCR::Storage` / `OSCR::Time` | POSIX files, clock, data directories |
| `OSCR::Power` / clock functions | Kernel sysfs and Retrode3 slot functions |
| `OSCR::UI`, `OSCR::Menus` | Noninteractive command dispatch and deterministic output |
| Database submodule | Fetch/generate supported database formats, independent of Arduino SD layout |

**Important blocker:** The rewritten NES, MegaDrive and SNES cores still directly manipulate `PORTx`, `DDRx`, and `PINx`. Namespaces and the current `OSCR::Hardware::PinControl` are not yet a per-console bus abstraction. Therefore, simply swapping in the new core files or defining `OSCR_ARCH_LINUX` is insufficient. A native kernel-backed cartridge bus interface is required. Prefer an upstream-accepted HAL API so future core updates can really be consumed without local code patches. Any interim build overlay must be independently tracked, automated, and verified rather than editing upstream sources by hand.

## Milestones

- [x] Establish isolated migration branch
- [x] Pin pristine upstream firmware as a Git submodule
- [ ] Implement Linux architecture target and compile-time smoke-test without Retrode3 hardware
- [ ] Design/implement NES bus operations, including PRG/CHR, mapper writes, and proper M2 timing
- [ ] Design/implement MegaDrive bus operations, including 16-bit words, SRAM/EEPROM, and Sonic 3 FRAM mode
- [ ] Design/implement SNES bus operations, including bank/mapping modes, SRAM, and clock
- [ ] Provide a deterministic, noninteractive CLI compatible with the web-UI integration
- [ ] Add mock kernel-device tests and CI with strict warnings and sanitizers where possible
- [ ] Test on real Retrode3 with known NES, MegaDrive, and SNES cartridges and validate dump hashes
- [ ] Validate save backup and read-only operations before allowing any save write or flash command
- [ ] Review changes and merge to `master` only after hardware acceptance

## Updating upstream

```sh
git submodule update --init third_party/oscr-firmware

# When intentionally testing a newer upstream revision:
git -C third_party/oscr-firmware fetch origin main
git -C third_party/oscr-firmware checkout <reviewed-upstream-commit>
git add third_party/oscr-firmware
git commit -m "Update pinned OSCR firmware"
```

Upstream's nested `databases` submodule currently uses an SSH GitHub URL. For an HTTPS-only test machine, initialize it with a temporary URL rewrite:

```sh
git -C third_party/oscr-firmware \
  -c url.https://github.com/.insteadOf=git@github.com: \
  submodule update --init databases
```

The legacy root `Makefile` still builds the old implementation. The migration build must be separately named until it reaches parity. Do not assume a successful compilation is proof of correct cartridge timing or power control.
