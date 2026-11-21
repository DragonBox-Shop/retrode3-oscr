# Automatic cartridge hardware profiles (experimental, Letux 7.3)

Status: **automatic SMS hardware probe and ROM read physically tested**
on 2026-11-21 using Action Fighter (USA/Europe/Brazil Rev 1), 128 KiB,
CRC32 **3658F3E0**, with `selection_mode:auto` and full-ROM
`verified:true`. The common host restored MD slot voltage (3300mV)
and both kernel gates (N/N) after a successful `probe_slot`.
The new product-code candidate detection in core 0.5.0 is **source/CI
only** until that separate feature is physically tested. Web browser
acceptance and reboot persistence remain separate tests.

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
The OSCR database contains **full-ROM CRC32 and names only**, not
mapper metadata or short ROM fingerprints. For tentative game name
candidates without dumping, `third_party/oscr-sms-product-codes.json`
contains 272 OSCR CRC records cross-referenced by full-CRC match to the
**CC0** MAME Master System software list, pinned to Git blob
`5410f81bc8e1900375d076f2f7ee42ee852c2c48`
(commit `d39e971ee65da1d1b0170da3ce187fc6168e71e1`).
The Python 3.4-compatible `tools/build-linux-sms-header-catalog.py`
generates the read-only candidate lookup. It relies on product numbers
encoded as BCD in the existing 16-byte Sega cartridge header; it reads
no additional mapper pages and performs NO mapper writes on probe.

If header product code AND declared size match known records, the core
reports possible ROM revisions with stable `sms-crc-XXXXXXXX` IDs.
For example Action Fighter product code 5055 has two known CRC variants
`3658F3E0` and `D91B340D`; generic ambiguity is `titles_only`,
`selection_required:false`, and the shared title is `Action Fighter`.
The header is not an identity proof and may contain inaccurate information.
Unknown codes remain `identification_status:unknown` without a false title.
Header size is still only a layout hint. The full streamed ROM CRC is the
only final game/revision verification. Explicit candidate mode rechecks the
header and reports `mismatch` if the selected revision does not match the
final full-ROM CRC. The original Sanni ROM read loops are unchanged.

For a single unambiguous supported header geometry the SMS probe marks
`selection_required:false` and `auto` chooses that same layout by
re-reading just the bounded header at read time. Unknown, conflicting
or unsupported header geometries require explicit v3 layout/override.

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
