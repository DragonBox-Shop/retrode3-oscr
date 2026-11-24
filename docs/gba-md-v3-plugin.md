# GBA-MD v3 ROM-only plugin (GBx-0011), hardware-gated

## Native automatic probe (2026-11-24 integration work)

The GBA core now declares the SAME v3 `hardware_access` mechanism as
SMS-MD. The shared CLI holds the bus lock, applies the GBA-specific
3.3V probe/read profile and `gbx_md_test=true`, calls the GBA
`fast_probe_json` callback and rolls back the hardware parameter and
voltage afterwards. The first mode-24 `pread()` of the GBA header
enters `retrode3_gbx_prepare()` in the kernel and performs the
previous Python `arm` direction preparation inline. **There must
never be a separate Python/native arm executable or a human-run arm
step** in the intended runtime. The new core profile has
`probe_priority=150` (SMS-MD uses 100) and has no cart write capability.

**Not yet safe to ship automatically:** the kernel's experimental
`bus->gbx_active` latch still blocks ordinary MD bus access after a
GBx read until module reload, and `slot_probe` requires MD sense to
report active, which the GBx adapter may not do. These must be
addressed and tested before enabling the core in normal auto-detect.
Automatically starting the kernel preparation with an inserted GBx
adapter also needs real electrical acceptance, particularly GPIO A19
(GBA IRQ/GB audio), possible other MD-core probes, and VCC restoration.
This integration must not simply delete the safety latch or blindly
drive the shared pin after an unsuccessful header check.

For now, the legacy Python arm instructions below remain historical
**bench-only** reference instructions, not the intended plugin runtime.
The C++ core's normal runtime performs no Python subprocess calls.



Status: **implementation and hardware-free ABI tests only**. This is an
opt-in, separately packaged core; no production auto-activation and no
save/EEPROM/flash writing. Do not install/enable for general users yet.

## Proven transport baseline

Kernel branch `retrode3-kernel/develop` contains experimental GBx MD
ROM transport modes 24/25 (random access) and 28/29 (GBA burst).
On 2026-11-23 a 16 MiB Shining Force `AF5P` GBA ROM was successfully
read at **3.3 V** using burst mode 28: CRC32 `07B06994`, SHA256
`8a8485e100d9747e44e3a4255058af7cc2a425709a7172fcddd05ad87cc2c1e1`,
124.915 s elapsed. A 1 MiB burst dump was byte-identical to the
random-access reference (`cmp`, CRC32, SHA256).

## Integration scope

The `gba-md` module exports the mandatory core descriptor and v1 entry
point plus generic v3 capabilities, fast probe, plan and stream callbacks.
Its physical slot is **`md`**, never `gba`. It uses the existing portable
`gbx/retrode2_readonly.h` GBA header validator based on Sanni's pinned
GameBoyAdvance ROM reader, and reads the data with bounded `pread()` from
`/dev/slot-md` using mode 28. The common CLI owns output filename,
atomic publication, CRC32, progress, host bus mutex and cancellation.
The module does no cartridge writes or stateful hardware configuration.

GBA header does not encode ROM size. A successful header probe returns
`selection_required:true` and offers layout IDs `gba-1m`, `gba-2m`,
`gba-4m`, `gba-8m`, `gba-16m`. The UI can render them generically via
v3 `read_layouts`; auto mode is intentionally refused. Generic manual
`override` supports exactly one `rom_mib` integer from 1,2,4,8,16.
32 MiB and mode 29 are implemented in the kernel but **not approved**
for this plugin until physical boundary testing. Verification stays
`not_checked` without an authoritative game-database match. SRAM, save,
flash and mapper writes are **not supported**.

## Essential electrical and runtime gate

The current kernel is still a **bench** build. It requires
`gbx_md_test=1`, and the operator MUST run the separate bench `arm`
operation with adapter/cartridge removed **before** inserting the
cartridge. Simply setting `gbx_md_test=1` is insufficient: the arm
read ensures all shared pins, including GBA IRQ, are high-Z.

The plugin checks that `gbx_md_test` is enabled and VCC reads exactly
`3300mV` before opening the MD slot. It does **not** turn either feature
on automatically: the generic host hardware-scope infrastructure does
not yet implement this safe empty-slot arm transition. Therefore the
core must remain **disabled** for normal use. Do not expose it to
automatic slot detection or the browser until the kernel switches to a
production-safe, reversible adapter selection profile.

Before any manual hardware acceptance test, remove adapter and cart,
reload the separately staged test `.ko` with `gbx_md_test=1`, run
`python3 tools/gbx-md-hwtest.py arm`, then insert the GBA cartridge.
Only after that, with the verified firmware and hardware present,
consider enabling the `gba-md` shared core for a supervised test.

No new CLI executable, Web-specific code or default production-package
installation is needed. `stage-gba-md-module` only copies the `.so` and
`.core` into the offline build stage, never enables it.

## Build and tests

```sh
make -f Makefile.migration native-gba-md-module
make -f Makefile.migration test-native-gba-md-module
make -f Makefile.migration stage-gba-md-module  # optional offline staging
./tools/preflight-ci.sh
```

Build/test on a full repo checkout with `third_party/oscr-firmware`
pinned and clean. The `RETRODE3_TESTING` test binary replaces sysfs
and `/dev/slot-md` with fake files. None of these mock tests validates
actual cartridge timing or USB power. Keep the actual `.so` on the
feature branch until the full preflight and physical end-to-end v3
CLI validation pass.
