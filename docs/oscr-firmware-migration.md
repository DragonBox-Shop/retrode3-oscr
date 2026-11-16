# Migration to the redesigned OSCR firmware

> Status: **experimental / scaffolding only**. The firmware in `third_party/oscr-firmware` is not yet compiled or executable on Retrode3. Do not deploy this branch to production hardware.

## Baselines

- Stable Retrode3 implementation: `master`, starting commit `3bbc215bfc1a972a53fb69716fb86ac94e07b941`.
- New upstream firmware: https://github.com/oscartreader/oscr-firmware
- Initial upstream revision: `e5a08326dfc8fff67193f7fc5a7bc36b5282b28c` (2026-11-11).
- Legacy upstream `sanni/cartreader` is archived; the original `Cart_Reader/`, `Makefile`, `oscr-cmd.*`, and `retrode3-lib.*` are retained for comparison.

## Non-negotiable goals

1. **Minimize changes to upstream cartridge cores; pristine upstream remains the reference.** All files underneath `third_party/oscr-firmware` are tracked unchanged as an upstream Git revision. A few narrowly scoped core modifications are acceptable when technically necessary, but must be versioned outside the upstream checkout as reproducible build overlays/patches; never manually rework every core on each upstream update. Prefer sending the tiny hooks upstream.
2. Retain Linux kernel slot ABI and the existing Retrode3 device library (`/dev/slot-nes`, `/dev/slot-md`, `/dev/slot-snes`) unless a separately coordinated kernel change is necessary.
3. Keep the command-line program usable non-interactively from a web interface. Return reliable exit status, machine-readable progress/status, and output-file paths.
4. Preserve `master` until builds, integration tests, and real-cartridge verification pass.

## Architectural assessment (2026-11-16)

The new upstream has moved from a large collection of Arduino `.ino` files to PlatformIO, C++20, and namespaces under `OSCR::`. Major pieces include:

| Upstream | Retrode3 Linux integration needed |
| --- | --- |
| `src/cores/{NES,MegaDrive,SNES}.cpp` | Keep cartridge logic upstream; permit small reproducible overlays for low-level bus boundaries when unavoidable |
| `include/arch.h`, `include/arch/syslibinc.h` | Introduce a Linux target/architecture path |
| `src/arch/avr/**`, `include/arch/avr/**` | Supply Linux equivalents rather than emulating AVR hardware |
| `OSCR::Storage` / `OSCR::Time` | POSIX files, clock, data directories |
| `OSCR::Power` / clock functions | Kernel sysfs and Retrode3 slot functions |
| `OSCR::UI`, `OSCR::Menus` | Noninteractive command dispatch and deterministic output |
| Database submodule | Fetch/generate supported database formats, independent of Arduino SD layout |

**Important blocker:** The rewritten NES, MegaDrive and SNES cores still directly manipulate `PORTx`, `DDRx`, and `PINx`. Namespaces and the current `OSCR::Hardware::PinControl` are not yet a per-console bus abstraction. Therefore, simply swapping in the new core files or defining `OSCR_ARCH_LINUX` is insufficient. A native kernel-backed cartridge bus interface is required. Prefer an upstream-accepted HAL API so future core updates can be consumed with zero or near-zero local core changes. Any necessary interim build overlay must be independently tracked, automated, and verified rather than editing upstream sources by hand.

## Porting policy: thin CLI wrapper + hardware adapter (decision 2026-11-16)

Our priority is **automatic mapping to the Retrode3 Linux kernel interface with the fewest possible upstream core changes**, not an absolute ban on patches.

Suggested layout:

```text
third_party/oscr-firmware/  upstream submodule, not edited
src/retrode3/cli/          CLI dispatch, machine-readable status and errors
src/retrode3/compat/       Linux replacements for Arduino/UI/Storage/Time
src/retrode3/hal/          Typed NES/MD/SNES bus API backed by retrode3-lib
patches/oscr-firmware/     Only minimal, deterministic core hooks if required
tests/                     Mock bus tests and upgrade checks
```

**Two different sorts of compatibility are needed:**

1. Most generic Arduino facilities can be provided as a **compile-time compatibility wrapper** without touching the firmware's cartridge algorithms.
2. The electrical cartridge bus must be translated as **complete semantic operations** (e.g. NES read PRG/CHR in specific mapper/timing modes, MD read/write word and FRAM toggles, SNES banked reads). Do **not** naively map individual AVR `PORTF`, `PINK`, and `DDRx` changes to individual Linux syscalls: the bus needs atomic timing, and a read of a virtual register is not enough to infer whether the core intends a PRG access, CHR access, mapper write, or another transaction.

Concrete examples in upstream commit `e5a0832`:
- NES `read_prg_byte()`, `read_chr_byte()`, mapper writes, and pulse-M2 variants encode different bus semantics. An MMC1 register-write comment explicitly requires approximately 33 ns between two control signals; this cannot be reproduced by userspace bit-by-bit syscalls.
- MegaDrive `readWord()` drives AVR ports directly, while its callers use word-addressing; Retrode3 `md_read()` expects **byte addresses** and has bus mode selections (`MD_MODE_ROM`, `MD_MODE_P10`, etc.). A single adapter must convert address units, byte order and modes carefully.
- SNES `readBank()` is not the only direct-bus path: `readLoRomBanks()`, `readHiRomBanks()` and header/probe functions also read ports directly. Those require routing or replacing with **bulk kernel reads** for reasonable performance.

**How to keep patches tiny:** First implement a typed, testable bus API and POSIX command-line/Storage/UI compatibility. Then audit **all** direct register accesses in the three enabled upstream cores, route a small number of I/O functions and bulk-read paths through the HAL via tracked overlays, and leave all mapper, ROM format, checksum and menu logic intact. If possible, contribute the platform hooks upstream so the overlays disappear later.

**Update acceptance criteria:** Checking out a newer upstream commit and re-running a deterministic build must either (a) succeed with the exact unchanged compatibility overlays and tests or (b) fail loudly and identify which small boundary changed. Never silently fuzz or best-effort-rewrite unknown source patterns; never mutate the submodule during an update. Gate destructive operations until real hardware has been tested. A Linux compile alone does not demonstrate electrical safety or correct output.

## Milestones

- [x] Establish isolated migration branch
- [x] Pin pristine upstream firmware as a Git submodule
- [ ] Implement Linux architecture target and compile-time smoke-test without Retrode3 hardware
- [x] Implement and mock-test the initial read-only NES/MD/SNES kernel bus adapter (not wired into OSCR yet)
- [x] Generate the preliminary web menu schema from upstream C++ arrays and NES mapper database (all actions disabled)
- [x] Add separate **runnable** hardware acceptance CLI for NES mapper 0 (NROM) and standard MegaDrive ROMs, with original OSCR NES database probing, safe output and mock tests. This is **not yet** the new C++ core port.
- [ ] Connect OSCR NES core to the kernel bus adapter, including PRG/CHR, mapper writes and proper M2 timing
- [ ] Design/implement MegaDrive bus operations, including 16-bit words, SRAM/EEPROM, and Sonic 3 FRAM mode
- [ ] Design/implement SNES bus operations, including bank/mapping modes, SRAM, and clock
- [ ] Provide a deterministic, noninteractive CLI compatible with the web-UI integration
- [ ] Add mock kernel-device tests and CI with strict warnings and sanitizers where possible
- [ ] Test on real Retrode3 with known NES, MegaDrive, and SNES cartridges and validate dump hashes
- [ ] Validate ROM dump and save backup/read-only operations first; implement save restore and Flash workflows in a separate later phase
- [ ] Review changes and merge to `master` only after hardware acceptance

## Web interface and recognition policy (2026-11-16)

- Preserve OSCR's **original automatic cartridge recognition** via the cartridge CRDB (for NES, `nes.json`). Do not replace it with a mandatory manual mapper choice.
- Allow **manual override** only when recognition fails or is wrong. Populate mapper/submapper and legal size fields from the same upstream database (`nes-mappers.json`) and the core's size tables. The currently pinned firmware's nested database revision is `87a48948c6a04260205f148d313614cc44e05cd5`.
- Generate stable menu IDs and guarded choices from upstream menu arrays; the first version is `tools/oscr-export-web-menu.js` plus `generated/oscr-web-menu.json`. The catalog describes actions; it **does not execute them**.
- Web UI calls should use a deterministic CLI command with explicit system/action and validated options, not Arduino navigation sequences. No shell interpolation, no arbitrary filesystem paths.
- Flash functions are **in scope**, but are implemented after stable cartridge ROM and save reads. They are present in the preliminary catalog as **disabled** write-risk actions.
- The `ReadBus` adapter under `include/retrode3/` and `src/retrode3/` is an isolated tested foundation. It does not power slots on or run new OSCR core code yet.

See [`docs/first-hardware-readtest.md`](first-hardware-readtest.md) for the exact test commands and the currently supported cartridge types. No hardware run has been performed on an actual Retrode3 yet.

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
