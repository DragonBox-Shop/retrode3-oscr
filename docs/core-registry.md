# Offline-preinstalled Retrode3 cores and enable/disable registry

Status: 2026-11-18, branch `feature/core-registry`.
[Offline core registry CI run 44221703495](https://github.com/DragonBox-Shop/retrode3-oscr/actions/runs/44221703495)
**passed**; real MIPS execution is pending.
**Registry and helper are development prototypes** until target MIPS tests
and the real operation ABI/packaged OSCR modules are completed.
All earlier approved SNES/MD ROM dumps remain in the separate acceptance
programs. The pristine pinned Sanni upstream submodule is unchanged.

## Shipping policy: all supported cores offline

Each Retrode3 SD-card OS image should ship with **all released, hardware-safe
cores preinstalled**, including the corresponding module, manifest and all
dependencies. No Internet is required to enable a core or read a game.
Unused modules stay on the card and can be switched on and off without
installation, downloads or rebuilding the CLI. Unfinished mapper/save cores
must not be advertised as usable in a release image.

An installed software core and a physical cartridge adapter are different:
slot/adapter presence must be discovered separately. Enablement is likewise
distinct from ABI compatibility and currently implemented operations.

### Apache-style directories

```text
/usr/lib/retrode3/cores-available/
    snes.so
    snes.core
    md.so
    md.core
    nes.so
    nes.core

/etc/retrode3/cores-enabled/
    snes.so -> /usr/lib/retrode3/cores-available/snes.so
    md.so   -> /usr/lib/retrode3/cores-available/md.so
```

Package-managed `cores-available` is read-only to unprivileged users.
The root-owned `cores-enabled` directory contains **only symlinks**, pointing
to the **exact absolute corresponding path** under `cores-available`.
Relative, redirected, mismatched and arbitrary user-created links are invalid
and do not cause a module to load. We do not `dlopen` the enabled symlink.
Other `.so` and `.core` contents are not accepted from user-supplied paths.

A package's `<id>.core` is a tiny static, root-installed metadata manifest:

```ini
id=md
name=Mega Drive
version=0.1.0-test
slot=md
```

The real package should use its actual released version. This is deliberately
separate from the `.so`: **disabled modules are never loaded**, including
possible shared-object initializers, but the UI can still show their names,
versions and slot requirements. The manifest and the loaded descriptor must
match for an enabled core to pass discovery ABI validation.

## What is implemented in this branch

The C++11 host binary `retrode3-cli` enumerates installed core manifests,
validates the adjacent module, checks trusted owners/modes and the enabled
link (if present), and emits one machine-readable NDJSON `cores` event:

```sh
make -f Makefile.migration native-core-registry
make -f Makefile.migration test-core-registry
build/migration/retrode3-cli --list-cores --protocol ndjson-v1
```

Example for one installed and enabled **metadata-only test module**:

```json
{"protocol":"retrode3-ndjson","version":1,"event":"cores","request_id":"cli","discovery_abi":1,"cores":[{"id":"md","name":"Mega Drive","version":"0.1.0-test","slot":"md","installed":true,"enabled":true,"status":"discovered","usable":false,"actions":[]}]}
```

If the same module is disabled, it remains present with `installed:true`,
`enabled:false`, `status:"disabled"`, `usable:false`, and `actions:[]`.
Other statuses include `invalid_link` and `incompatible`. A valid
**discovery** descriptor is not an implementation of any cartridge operation.

The privileged, fixed-path helper only changes symlinks (and can be
installed as `/usr/sbin/retrode3-corectl` by the future OS package):

```sh
# Run as root, after the image/package installer has created both directories
retrode3-corectl enable md
retrode3-corectl disable md
```

For the development tree, the helper currently builds as
`build/migration/retrode3-corectl`. It refuses path arguments, invalid
core IDs, missing or untrusted packages, and links whose target is not the
expected installed module. Calls are idempotent. It does not touch module
files or download anything. The normal CLI also refuses caller-supplied
directories. Only the test-specific builds expose directory overrides.

**Important:** No `retrode3-core-snes`, `retrode3-core-md`, or
`retrode3-core-nes` production `.so` module has been installed by this
commit. The tests use synthetic modules, not original Sanni OSCR adapters.
The static `liboscr-*-core.a` acceptance libraries are not dynamically
installable and must not appear in the new registry.

## OS image and web integration (not yet implemented)

The `DragonBox-Shop/retrode3` image build should package all released
core adapters and manifests, install their dependencies, create the two
directories securely and create default enabled symlinks for production-ready
cores. Unfinished cores may be present but must remain disabled and never
expose unverified operations.

The web backend runs the **common** `retrode3-cli --list-cores` to show
installed/disabled/compatible states, and enables toggles only through a
tightly scoped privileged service or authorised helper. **Never** give the
web process arbitrary root shell access, writable library directories,
unrestricted package-management rights, or raw symlink path arguments.
Installation and updates may still use signed Debian packages later;
ordinary enable/disable never requires apt or internet access.

## Next steps

1. Test registry and helper under C++11 on real Retrode3 MIPS; verify that
   missing directories show an empty list without cartridge access.
2. Implement the shared operation ABI and common CLI job runtime with
   the existing SNES NDJSON behavior (progress, cancel, CRC, atomic output).
3. Wrap physically tested SNES and MD libraries in independently loadable
   modules; real adapters must expose only genuinely safe operations.
4. Package the completed modules in the SD-card image and connect the
   available/enabled toggles to the independently developed web backend.
5. Add NES and other Sanni cores and adapter capability detection, each
   requiring its own hardware verification before being marked usable.

Software status and hardware detection must remain separate. **Installed
does not mean enabled; enabled does not mean compatible; compatible does
not mean the specific ROM/save operation is implemented or safe.**
