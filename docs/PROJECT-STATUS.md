# Retrode3 project status and next-step contract

Updated: 2026-11-17
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
