# Retrode3 project status and next-step contract

Updated: 2026-11-18
Working branch: `migration/oscr-firmware` (do not merge to master by default).
Purpose: durable, source-controlled project memory for contributors and assistants.
Read [AGENTS.md](../AGENTS.md) and the
[OSCR migration architecture](oscr-firmware-migration.md) before editing.

## Project goal

Retain our own Retrode3 Linux CLI and web UI. Compile original Sanni OSCR
cartridge-core algorithms as separately linkable libraries, including the
original mapper and special-chip logic where hardware supports it. Use a
common Retrode3 HAL backed by the kernel slot drivers. Our own cartridges,
Retrode2-style adapter/plugin support and other extensions should use the
same core interface. Do not duplicate mapper algorithms already maintained
upstream. Keep `third_party/oscr-firmware` pinned and untouched; contribute
general fixes and useful HAL abstractions upstream after review.

Related repositories:
- `DragonBox-Shop/retrode3-oscr` (this repository; active migration here)
- `DragonBox-Shop/retrode3-kernel` (GPIO, slot devices, timing)
- `DragonBox-Shop/retrode3` (Debian-based OS/web frontend)
- `DragonBox-Shop/retrode3-hardware` (boards and adapters)
- `oscartreader/oscr-firmware` (original new OSCR upstream)

## Hardware verification: observed, not inferred

- **Native C++ MegaDrive stage-1 linear read-only path verified on Retrode3
  2.9.5** with Snake Rattle n Roll (Europe). The upstream-derived
  `readWord()` probe returned 524288 bytes by header, checksum `2152`,
  serial `GM MK-1541 -50`, region `E`, mapping `linear`.
  Two physical native partial-CLI dumps each wrote exactly 524288 bytes and
  `cmp` confirmed they were byte-identical. Full-ROM CRC32 `543BED30`
  matches the separate pinned original
  [OSCR MD database](https://github.com/oscartreader/oscr-databases/blob/87a48948c6a04260205f148d313614cc44e05cd5/data/md.json)
  entry `Snake Rattle n Roll (Europe)`. SHA256 of the first dump:
  `26643484b639a10e0f889940a1556acbd8f9bcddb6de5b5a0ff2167104605200`.
  The CLI still reports `verified:null` because native database matching
  is **not yet implemented**. The full upstream `getCartInfo()` and
  `readROM()` have **not** been ported.
- The MegaDrive hardware testing previously exposed a genuine GPIO/OE
  settle-time problem in the kernel driver. The kernel was patched to delay
  sampling after asserting OE. Avoid ignoring hardware timing while porting
  mapper and special-chip control.
- Python read-only SNES LoROM: Super Turrican (Europe), 524288 bytes,
  OSCR CRC32 `D0A1BF12`. Two physical dumps were compared byte-for-byte
  and were identical.
- Python read-only SNES HiROM: Terranigma (Germany) (Rev 1), 4194304 bytes,
  OSCR CRC32 `6FB8A4DF`. Physical dump verified by OSCR CRC32.
- **Native C++ OSCR test runner compiled on actual Retrode3 2.9.5 hardware**
  after installing the C++ compiler and fixing a C `case` declaration in
  `retrode3-lib.c`.
- **Original upstream C++ `checkcart()` verified on real hardware** with
  Terranigma: `hi`, 4194304 bytes, chip type 2, checksum `3856`,
  title `TERRANIGMA D`.
- **Original upstream C++ `readHiRomBanks()` verified on real hardware**:
  `/tmp/terranigma-oscr-cpp.sfc` compared byte-for-byte equal to the
  Python reference `/tmp/terranigma-germany-rev1.sfc`; full CRC32
  `6FB8A4DF`.
- Physical native C++ LoROM readback comparison is **not yet reported**.
- Measured 4 MiB Terranigma HiROM on real Retrode3: original OSCR C++
  **164.49s** real / 0.00s user / 31.32s sys, Python **170.68s** real /
  2.84s user / 31.75s sys. C++ was ~3.6% faster in these single runs;
  both share the kernel bus and larger hardware timing optimizations remain.

## Native MegaDrive stage 1 (feature branch; CI and standard linear ROM hardware verification passed)

The isolated `feature/native-md-stage1` branch adds a generated static
`liboscr-md-core.a` from the pinned original `MegaDrive.cpp` methods
`readWord()` (AVR bus boundary replaced with the existing typed Linux HAL)
and `copyToRomName()` (unchanged). The source generator hard-fails on
unexpected changes to the upstream methods. The submodule is not edited.

`oscr-md-native` currently supports a read-only standard SEGA-header probe
through the upstream-derived `readWord()` and linear ROM dumping using the
already tested kernel `MD_MODE_ROM` bulk-read interface. It reports CRC32
and publishes files without overwrite. Explicitly excluded: 32X, SVP,
Sonic & Knuckles lock-on, ROMs above 4 MiB, mapper writes, RAM, EEPROM,
flash and special modes. This is **not** the original complete MD
`getCartInfo()` or `readROM()`; those require a separate guarded HAL port.
Automatic size and name in this partial stage come from the raw standard MD
header and one upstream naming helper, not the full upstream title/size
corrections, mapping or database logic.

Build and mock tests: `make -f Makefile.migration test-native-md` and
`make -f Makefile.migration native-md` after submodule init. The dedicated
CI workflow is `.github/workflows/native-md-migration.yml`. Dedicated workflow [run 44216393549](https://github.com/DragonBox-Shop/retrode3-oscr/actions/runs/44216393549)
**passed** including native C++ mock tests and Linux link against the C slot
library. Native `readWord()` physical header probe and two bit-identical
512-KiB dumps of Snake Rattle n Roll (Europe) have passed on real hardware;
full CRC32 `543BED30` matches the pinned original OSCR MD database entry.
Native CLI database lookup and the complete upstream MegaDrive reader
remain pending. Reproduce with [MD stage-1 hardware instructions](oscr-linux-md-stage1.md). Never
start by testing a write-dependent special cart.

## Shared native host architecture (decision 2026-11-18)

**All console cores share one Retrode3-owned CLI and NDJSON v1 runtime.**
Progress updates, cancellation, checksums, safe/atomic files, request IDs,
protocol error reporting, per-slot job exclusivity, command validation and
capability discovery belong exclusively to this common host, **not** to
individual Sanni cartridge cores. Never reimplement these separately in
NES, MegaDrive, SNES or add them to the upstream checkout. The original
Sanni ROM/save/mapper algorithms should remain pristine except for small,
deterministic GPIO-to-typed-HAL substitutions. POSIX/OSCR Storage,
ProgressBar, UI, Power and Time shims call shared host services.

The two native per-console binaries currently in this repository are
**acceptance prototypes**. `oscr-snes-native` already implements NDJSON
v1 and is physically verified; `oscr-md-native` is a one-shot JSON
acceptance runner only. Neither is the target production architecture.
Extract/reuse the existing `ndjson_protocol` parser in a single common
Retrode3 executable, then register each static core library using a typed
`probe / read_rom / read_save / capabilities` interface and host callbacks
for progress/cancel/output. Core capabilities must distinguish implemented
features from upstream-declared and physically-verified features; never
advertise unsupported mapper/save writes. The web frontend/backend is being
developed separately in the Retrode3 OS project and should not require
per-core process/protocol special cases.

**Next implementation sequence:** first establish the common host/core API
and move the proven SNES/MD runtime wrappers to it without changing the
working hardware paths; then add the original MD metadata/mapper/save
algorithms behind that adapter. NES follows MD. Keep CI tests and real-cart
read-only checks at each step. No claim that this runtime is implemented
until code and tests demonstrate it.

## Offline-preinstalled core registry (feature branch; CI and MIPS acceptance pending)

The SD-card image is intended to include **all released, hardware-safe
cores** and dependencies offline. Runtime enablement is separate from
installation: package-installed `<id>.so` and static `<id>.core` manifests
live in `/usr/lib/retrode3/cores-available`; root-managed, exact-target
symlinks under `/etc/retrode3/cores-enabled` determine which modules
may be loaded. This follows Apache's sites-available/sites-enabled model.
An installed but disabled module remains discoverable in the CLI output,
with `enabled:false` and `status:"disabled"`, and is **never dlopened**.
The root-only `retrode3-corectl enable|disable <id>` manipulates only these
links. The UI's privileged backend bridge is not implemented in this repo.

The development branch `feature/core-registry` now has a C++11 discovery
CLI, a versioned descriptor ABI, manifest parsing, strict symlink target
checks, and synthetic mock-install/enable/disable tests. All discovered
modules remain `usable:false` with no actions until a safe operation ABI
and genuine OSCR `.so` adapters exist. The actual production Cores and
image packaging **do not yet exist**. CI and MIPS hardware verification
must be recorded separately. This implementation does not touch the pinned
upstream OSCR source or the physically verified SNES/MD acceptance binaries.
See [core registry contract](core-registry.md).

## MegaDrive next-stage scope: all feasible special carts and saves (2026-11-18)

The goal is broader than standard linear ROMs: integrate the **original**
Sanni `MegaDrive.cpp` cartridge algorithms and mapper handlers where the
Retrode3 kernel and electrical hardware actually support their bus semantics.
Do not claim universal compatibility without individual cartridge evidence.
The current partial native library does not yet include these functions.

Pinned upstream integrates saves **inside the MegaDrive core**: `getCartInfo()`
identifies SRAM arrangements (low/odd, high/even, both byte lanes), serial
EEPROM and various special cases; `readSram()`, `readEEP()`, and the
Sega-CD backup RAM cartridge `readBram()` are separate *actions* within
MegaDrive, not independent console cores. `writeSram()`, `writeEEP()`,
`writeBram()` and flash programming also exist upstream but must remain
unavailable until an explicit, separately reviewed restore/write stage.

Planned incremental integration:
1. Separate pure metadata/header recognition from `getCartInfo()` side
   effects. Its upstream implementation invokes cartridge power transitions
   and, for Sonic & Knuckles plus Sonic 2, `writeSSF2Map()`; do not run this
   unguarded for a generic probe.
2. Port the remaining upstream ROM and mapper logic using small strictly
   verified source overlays and **typed kernel-level bus transactions**:
   banked SSF2 / expanded ROMs, lock-on, Realtec, 32X and SVP as electrically
   feasible. Keep tested linear reads as a regression reference. Each mode
   needs a separate mock and physical CRC/byte comparison.
3. Implement **save backup only**, with detected low/high/both SRAM wiring,
   EEPROM protocols, Sonic 3 FRAM and Sega-CD backup RAM as separately
   capability-gated operations. An SRAM/FRAM *read* can still require a
   mapper/control-register **write** to expose memory; this must be explicit,
   reviewed for timing and save integrity. `retrode3-lib.h` warns that the
   generic `MD_MODE_TIME` path for Sonic 3 FRAM is glitchy and even reads
   can lose data; use the dedicated `MD_MODE_ENSRAM` / `md_enablefram()`
   design only after confirming the cart identity and kernel behavior.
4. Save restore, flash/erase, unknown hardware and risky mapper probing
   remain disabled until separate electrical review, backups and approval.

All systems should eventually expose these actions via the same capability
registry and NDJSON model. **Do not equate read-only user intention with
zero electrical writes**, and do not probe arbitrary unknown mappers by
writing control registers.

## Current implementation and limitations

`make -f Makefile.migration native-snes` now builds:
- `build/migration/liboscr-snes-core.a`, an independent static archive
  generated reproducibly from the pinned original OSCR C++ `checkcart()`,
  `readLoRomBanks()`, and `readHiRomBanks()` methods;
- `build/migration/oscr-snes-native`, our own Linux read-only CLI linked
  against that archive, plus `retrode3-lib.c`.

The generator `tools/build-linux-snes-core.py` has a Python 3.4-compatible
source reading/writing path. The native partial stage uses C++11 on old
Debian/Letux MIPS. CI has mocked LoROM/HiROM tests and an actual Linux
compile. Upstream C++ files are never modified inside the submodule.

**Not implemented:** native OSCR CRDB lookup and alternate sizes, most
special-chip and mapper paths, save RAM, NES/MD native OSCR cores, common
plugin registry, a browser-facing backend bridge and production integration.
The earlier Python acceptance reader remains a trusted test reference.
**New:** opt-in `--protocol ndjson-v1` emits read progress and results,
`--interactive` receives validated JSON commands on stdin (probe, read_rom,
cancel, get_capabilities), including cancellation between core file writes.
Existing one-shot CLI JSON remains the default. Output bytes, not upstream
ProgressBar display callbacks, drive progress. No candidate selection or
database lookup is claimed in this native stage.

## Agreed web / CLI interaction model (native transport prototype implemented; backend pending)

The Retrode3 binary must remain controllable by our own web backend.
Avoid bundling upstream Arduino menus into the executable. Use one generic
core-capability and job model for Sanni's cores and our own adapters.

**Implemented SNES binary protocol:** see [NDJSON v1 contract](ndjson-protocol-v1.md).
The web frontend must use only currently implemented features and must not
interpret the future examples below as current commands.

**Recommended transport:** opt-in versioned NDJSON (`--protocol ndjson-v1`)
on stdout for machine-readable events, and newline-delimited JSON commands
on stdin for long-lived interactive jobs. stderr is diagnostic text only.
Keep traditional one-shot commands for shell users and compatibility.
The web backend owns a process/job manager and exposes authenticated
HTTP APIs plus SSE (or WebSocket if bidirectional latency requires it).
The browser does **not** launch the binary or access cartridge devices.

Suggested event/command types, to be stabilized in a separate protocol spec:
- `probe_result`: system/core ID, cartridge identity and header, candidate
  list with stable selection IDs, mapper/submapper/mapping, sizes, confidence,
  capabilities and supported overrides.
- `input_required`: choose one identified candidate, mapper, ROM size,
  or another explicitly permitted parameter. Include a session/job ID,
  prompt ID and allowed choices. A timeout or disconnect must not cause
  hidden defaults or writes.
- `command`: `select`, `read_rom`, `cancel`, optionally `resume` when
  independently verified. Validate every command against capabilities.
- `progress`: `job_id`, `phase`, `bytes_done`, `bytes_total`,
  optional bank, rate, percentage and ETA. Report **actually completed
  output bytes**, not inferred address changes. Rate-limit event emission
  so slow hardware is not overwhelmed. Ensure stdout is line-buffered or
  flushed; never mix human logs with NDJSON.
- `result` / `error`: final path, bytes, checksums, verification status,
  stable machine-readable error codes, and job completion state.
- Future `get_capabilities`: discover available cores, safe mapper options,
  input fields, ROM/SRAM operations and hardware-adapter requirements.

For progress in the existing native stage, the most reliable initial
counter is successful bytes written by `OSCR::Storage::File::write()`;
upstream `ProgressBar::advance()` currently advances once per bank
with its own scaling and is not yet suitable as an exact byte counter.
The backend translates `progress` events to SSE, keeping the frontend
independent of the console-specific reader.

### Unknown cartridges / advanced mapper work

- If OSCR finds multiple candidates, show them for explicit user selection.
- If OSCR finds no match, allow a validated advanced form for mapper,
  submapper, ROM size, banking and other supported core-specific choices.
  Do not silently guess a mapper or claim the ROM is verified.
- A future *mapper sweep* is a **separately designed diagnostic mode**, not
  a blind loop through every mapper. Some mapper probes require writes,
  register changes, clock pulses, RAM access or special voltages. Define a
  safe read-only allowlist and hardware capability checks before attempting
  it, preserve per-attempt diagnostics, and avoid overwriting outputs.
- Game save/flash writes must be explicit, separately guarded operations.
  Never mix them into a generic read-only scan.
- Use per-slot process exclusivity, cancellable jobs, clean partial-file
  handling, safe pin/power shutdown and bounded input/output paths.

## Next work, in order

1. Complete the native C++ LoROM physical cross-check against the verified
   Super Turrican reference, if desired.
2. [Done; CI + real hardware] The native CLI supports versioned NDJSON
   with byte-accurate progress, interactive commands, cancellation between
   chunk writes and cleanup, preserving one-shot JSON. Physical hardware
   confirmed progress, result, probe, capabilities, and cancellation; byte
   identity confirmed for the streaming dump.
3. Implement the web backend's job lifecycle, candidate selection and
   progress streaming to the browser, starting with the known SNES path.
4. Integrate the original OSCR CRDB and NES/MD cores through the same
   library/HAL interface; add safe mapper overrides and system capabilities.
5. Build Retrode2-style plugin-adapter support on the same core interface,
   checking actual physical wiring and timing first.
6. Add special-chip and SRAM paths with carefully reviewed electrical
   behavior; validate each on physical cartridges before deployment.

## Hardware adapter identification investigation

See [SNES adapter ID audit](snes-plugin-id-audit.md). Legacy Retrode2
adapters connect via MegaDrive, while many Sanni expansion adapters use
the 62-contact SNES cartridge slot. On Retrode3 3.0 several extension
contacts are unconnected, but they cannot be treated as globally spare:
published community adapter designs repurpose SNES contacts, including
pin 31 for an optional Jaguar-adapter signal. No resistor-ID pin is
selected. Do not build a PCB revision on an assumption of universal
pin availability; establish an exact cross-adapter pin matrix and
voltage/signal safety first.

## Development and Git hygiene

Avoid one commit per edited file. Work on a cohesive change set locally,
run mocks/tests and compile checks first, then commit the verified feature
as a unit. Squash related development commits when merging a finished PR.
Do not rewrite published branch history or force-push without coordination.
`master` remains protected from experimental replacement work.

For a new chat: ask the assistant to fetch this file, `AGENTS.md`, and
the relevant current GitHub source/CI results, then continue the next
pending item. This prevents loss of detail when a conversation fills up.
