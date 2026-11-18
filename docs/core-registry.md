# Installable Retrode3 cartridge cores: discovery registry v1

Status: 2026-11-18, branch `feature/core-registry`. This is a discovery
milestone, **not** the full common job runner or an installable SNES/MD release.

## Goal

Behave like RetroArch's independent installed cores, while keeping the
Retrode3 Linux kernel/HAL and the original pinned Sanni OSCR firmware clean:

- The single Retrode3-owned `retrode3-cli` discovers software core packages
  at runtime, with no hard-coded system menu.
- An administrator may install or remove optional `.deb` packages
  (`retrode3-core-snes`, `retrode3-core-md`, later NES and more).
  Installation/removal is managed by Debian/apt/dpkg in the OS repository,
  **not** by the web-facing CLI or by raw internet downloads.
- Each package will install a Retrode3-owned adapter `<id>.so` under
  `/usr/lib/retrode3/cores`. The adapter wraps the pinned original OSCR
  cartridge library and exposes a versioned host ABI; upstream source stays
  unmodified. Updating or removing an adapter does not require relinking the
  common executable.
- The backend queries the CLI and displays only *usable* installed cores.
  A detected physical cartridge adapter/slot is a separate concept; do not
  confuse software `core` packages with hardware plugin adapters.

## Implemented now: metadata discovery (no cartridge access)

Build:

```sh
make -f Makefile.migration native-core-registry
make -f Makefile.migration test-core-registry
build/migration/retrode3-cli --list-cores --protocol ndjson-v1
```

The CLI scans a fixed `/usr/lib/retrode3/cores` directory for `<id>.so`
objects. It requires a root-owned, non-group/world-writable directory and
files, refuses directory/file symlinks, loads modules with local symbol scope,
and checks the `retrode3_core_describe_v1` symbol, ABI version, struct size,
and metadata identity. It does **not** read the cartridge, install packages,
run mapper callbacks, or enable any read/write operation.

The normal binary **does not accept** a caller-provided core directory. Only
the separate `retrode3-cli-mock` test build permits `--core-dir` for its
temporary test fixtures. Unreadable or incompatible modules are excluded
from active listings; diagnostics go to stderr, never stdout.

Example output with a package installed and a valid metadata-only module:

```json
{"protocol":"retrode3-ndjson","version":1,"event":"cores","request_id":"cli","discovery_abi":1,"cores":[{"id":"md","name":"Mega Drive","version":"0.1.0-test","slot":"md","installed":true,"status":"discovered","usable":false,"actions":[]}]}
```

If none are installed (as on a clean system), the `cores` array is empty.
The existing experimental `liboscr-snes-core.a` and
`liboscr-md-core.a` are still statically linked acceptance components;
they are **not** dynamically installable packages and must not be reported
as installed by this new interface. The mock `.so` files compiled in CI are
test fixtures only, not installable or runnable OSCR cores.

The `installed` flag reports a discoverable module, **not usable hardware
support**. The `usable:false` and `actions:[]` markers are intentional;
they must remain until a compatible *operation ABI*, common job dispatcher,
callback/bus safety validation, and actual core adapter are implemented and
tested. A UI should only enable actions explicitly reported as usable.

## Next milestones

1. Add a versioned **operation ABI** for `probe`, `read_rom`,
   `read_save`, etc., shared host callbacks for byte-accurate progress,
   cancellation, atomic file publication, error codes and optional CRDB
   verification. Preserve the existing SNES NDJSON v1 behavior.
2. Package the already physical-verified native SNES and MD partial runners
   as dynamically loadable Retrode3 adapter modules, with no new upstream
   source changes. Physical regression tests must still pass.
3. Add `get_capabilities` / `list_cores` to the common interactive NDJSON
   runtime with the same versioned response contract, then expose them
   through the separate web backend.
4. Add .deb installation/removal and upgrade transactions in the
   `DragonBox-Shop/retrode3` OS repository; never let an unauthenticated
   webpage load arbitrary libraries or manipulate package management.
5. Implement independent cartridge-adapter identity/slot availability,
   keeping user-facing status separate from software core availability.

The long-term UI model should explicitly distinguish **installed**, **ABI
compatible**, **usable**, **hardware compatible**, and **cartridge present**.
Do not advertise a core's upstream capabilities before they are actually
implemented and safe on Retrode3.
