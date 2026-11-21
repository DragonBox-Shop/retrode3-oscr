# Automatic cartridge hardware profiles (experimental, Letux 7.3)

Status: **source implementation and offline CI only**. The original SMS
hardware acceptance is Action Fighter (USA/Europe/Brazil Rev 1), 128 KiB,
CRC32 **3658F3E0** using the manually gated experimental Sega mapper.
The new automatic transition lifecycle has **not yet been accepted on the
physical Retrode3**.

## Contract and ownership

- The root-owned 4-field `.core` discovery manifest remains unchanged.
- Each v3 plugin describes `hardware_access.version=1` in its own
  `capabilities_json`. The schema declares `probe` and `read` profiles:
  permitted kernel module parameters, optional slot voltage, and optional
  private environment opt-ins. It may declare `probe_priority` (0..1000)
  for specific adapter recognition before broad system probes on the same
  physical slot. **Nothing is hardcoded to SMS in the shared CLI.**
- The shared CLI holds the existing exclusive bus lock. It applies the
  declared phase profile before touching the core's probe or read callbacks
  and restores all previous settings on every normal completion, failure,
  and cancellation. A kill -9 or power loss cannot run userspace cleanup,
  so kernel reset/defaults and boot verification remain necessary.
- Web v2 already calls the CLI's generic `probe_slot` and handles its
  `slot_probe_result`. No per-console Web branches are required.
- The experimental 7.3 kernel exposes the two tightly gated test module
  parameters as mode 0644; real bus/slot/value/5V restrictions remain
  kernel enforced. The corresponding Web SysV init draft grants its
  reader group access only to these module parameters and slot VCC files.

For SMS-0007 on MD, the **probe** profile requests 3.3V, MD CE high-Z,
and mapper writes disabled. The **read** profile requests 5V, high-Z,
the guarded Sega mapper gate and the preexisting private SMS opt-ins.
The common CLI changes these states only for the selected operation.
SNES applies MD CE high-Z while probing/reading, and MD/NES explicitly
disable the legacy bridge gates for their normal operations.

## Source provenance and database

The SMS ROM-size and ROM-bank loops are generated from the exact pinned
original OSCR `src/cores/SMS.cpp` by
`tools/build-linux-sms-core.py`. AVR pin, SD and progress operations
are replaced ONLY at reviewed HAL/host boundaries. The upstream
submodule is unchanged.

`tools/build-linux-sms-catalog.py` generates a C++ CRC lookup from a
vendored, immutable snapshot of
`oscartreader/oscr-databases/data/sms.json`, upstream commit
`58ee0dd4770d5618d55efdd84ef355edbefcae99`, Git blob
`c34d43076180413cb7857a1db0835c4559ac6c7d`.
There are 675 entries, including Action Fighter CRC32 `3658F3E0`.
The database contains **full-ROM CRC32 and names only**, not mapper
metadata or short fingerprints. Therefore probes report cartridge
layout from a bounded read but do not infer the game title. The full
streamed ROM CRC identifies and verifies the game **after** dumping.

The pinned Sanni SMS reader covers 8/16/32 KiB linear and the standard
Sega 16-KiB ROM bank mapper (48/64/128/256/512/1024 KiB). Codemasters,
Korean and other non-Sega mappers are **not** provided by this Sanni
source. Do not claim support until the upstream-compatible algorithm,
HAL/register path, and physical verification are implemented.
Save-RAM read/write and restore remain a separate follow-up milestone.

## Developer acceptance sequence

1. On Arch Linux, use Podman image
   `localhost/retrode3-mips-builder:latest` and prepared kernel tree
   `~/retrode3-timing/letux-7.3-rc5`. Use feature
   `feature/sms-auto-hardware-profile-7.3` and build **only**
   `make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- M=drivers/misc/retrode3 modules`,
   inside the existing bind-mounted cross-build container. Preserve
   `.config` and `Module.symvers`, verify `modinfo -F vermagic`.
2. Copy test `retrode3.ko` to a separate filename on the device.
   Never replace the installed module before successful hardware tests.
3. Build and install the shared CLI and `sms-md.so` from this draft
   branch. Enable the plugin with `retrode3-corectl` and restart Web
   service after loading the experimental module so its narrow SysV
   group permissions apply to the new sysfs inodes.
4. Run `./tools/preflight-ci.sh` in the complete source checkout.
   The GitHub CI covers generated SMS source, catalog consistency,
   v3 hardware scopes in fake sysfs, SNES/MD/NES regressions and Web
   mocks. These tests do not replace the MIPS or electrical tests.
5. With only an SMS cart in the adapter, confirm the generic
   `probe_slot md` picks `sms-md` without manually toggling kernel
   params; confirm VCC and flags restore to their previous values.
   Test SNES probe then NES and normal MD without driver reloads.
6. Read Action Fighter through the normal Web v3 selection or the
   existing NDJSON v3 `read_rom` command, **without any shell exports
   or echo to VCC**. Expect 131072 bytes, CRC32 3658F3E0, catalog
   verification true and the full title. Repeat and compare byte-for-byte.
7. Exercise failure, cancel and removal cases, plus Web service
   permissions on real hardware. Keep the PR draft until these pass.

For a manual existing-NDJSON command (no console-specific CLI flag):

```sh
printf '%s\n' '{"command":"read_rom","request_id":"sms-test","core":"sms-md","output":"/tmp/sms-auto.sms","selection":{"version":1,"schema_id":"sms-md-layout-v1","mode":"layout","layout_id":"sega16k-128"}}' | /usr/bin/retrode3-cli --protocol ndjson-v1 --interactive
```

Expected source-only test targets:

```sh
make -f Makefile.migration test-generic-selection-v3
make -f Makefile.migration test-sms-md-module
```
