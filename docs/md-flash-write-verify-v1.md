# MegaDrive Flash: shared CLI and normal core capability

## Runtime architecture

All Cartridge features use the **same** executable:
`/usr/bin/retrode3-cli` and the same versioned, dynamically loaded
`md.so` (built via `make -f Makefile.migration native-md-module`).
No separate lab core, elevated Web process, privileged Flash helper,
root opt-in, sudoers file or extra confirmation flag is required.

The MegaDrive core implements the optional
`include/retrode3/core_flash_v1.h` interface:

- `identify` (CFI and manufacturer/device ID)
- `erase_sector` (native aligned sector erase)
- `program` (bounded x16 programming)
- `readback` (array read, for full ROM verification)

The core exports `retrode3_core_get_flash_operations_v1` alongside the
existing ROM read ABIs. The common host discovers the Flash capability
and advertises `probe_flash`, `write_flash`, `verify_flash` in the
normal `--list-cores` and `get_capabilities` responses.

## Customer-facing workflow

1. User opens **Write Cartridge**, inserts a cart and selects its core.
2. **Detect Flash** reads manufacturer/device ID and CFI geometry.
   **Unrecognized or unsupported carts must not be writable.**
3. User selects a ROM from that system's existing logical ROM folder.
4. In **Write Cart**, independently select **Erase**, **Write** and
   **Verify**, then click one Start button. Verify can run **1..99**
   complete byte-for-byte passes. All three are selected by default,
   but any nonempty combination is supported.
   - Erase only, without an uploaded ROM: erase all detected Flash sectors.
   - Erase with an uploaded ROM: erase only sectors overlapping the image.
   - Write without Erase: program into already-erased Flash (normal
     1-to-0 bit transition rules still apply).
   - Verify without Erase or Write: compare a previously written image.
   - No Verify: report `not_checked`, **never** `verified`.

The Web backend invokes the same shared CLI under its normal
`www-data` identity as for ROM reading. The SysV runtime grants access
to the cartridge device and scoped voltage control with the
`retrode3-reader` group. The kernel checks slot, selected 5V connector
level, CFI geometry, supported Flash identity and valid write addresses
on every destructive operation.

This is standard device compatibility, not a human authorization gate.
Identifying a supported cartridge is a technical prerequisite to writing,
not an operator opt-in. Insertion detection alone does **not**
initiate Flash commands or erase anything.

## Shared CLI commands

```sh
retrode3-cli --protocol ndjson-v1 --core md --action probe-flash

# Independent stages, 3 complete Verify passes:
retrode3-cli --protocol ndjson-v1 --core md --action flash-operation \
  --erase --write --verify-passes 3 \
  --source /usr/local/games/ROMs/MD/game.md

# Erase entire detected Flash (no ROM required):
retrode3-cli --protocol ndjson-v1 --core md --action flash-operation --erase

# Standalone repeated Verify without erase/program:
retrode3-cli --protocol ndjson-v1 --core md --action flash-operation \
  --verify-passes 5 --source /usr/local/games/ROMs/MD/game.md

# The original one-shot compatibility aliases still exist:
retrode3-cli --protocol ndjson-v1 --core md --action write-flash \
  --source /usr/local/games/ROMs/MD/game.md
retrode3-cli --protocol ndjson-v1 --core md --action verify-flash \
  --source /usr/local/games/ROMs/MD/game.md
```

These commands need **no** `sudo`, `--confirm-erase` or kernel
test-parameter toggles when the caller belongs to the normal device
access group. The host acquires the shared
`/run/lock/retrode3/bus.lock`; do not take a second external
`flock` around the CLI.

## Genuine phase progress

The normal NDJSON v1 `progress` event reports actual completed work,
using the existing request ID and these phases:

```json
{"protocol":"retrode3-ndjson","version":1,"event":"progress","request_id":"job1","phase":"erase","units":"sectors","units_done":3,"units_total":71,"percent":4}
{"protocol":"retrode3-ndjson","version":1,"event":"progress","request_id":"job1","phase":"program","units":"bytes","bytes_done":65536,"bytes_total":4194304,"percent":1}
{"protocol":"retrode3-ndjson","version":1,"event":"progress","request_id":"job1","phase":"verify","units":"bytes","bytes_done":65536,"bytes_total":4194304,"percent":1}
```

Each phase begins at 0%, reaches 100% on completion and is labeled in
the shared Web progress panel. A write job does not report success until
the entire selected ROM has been read back and compared byte-for-byte.
The `flash_verification` result records verified bytes or the first
differing byte offset. The older ROM-database `verification` field on
`read_rom` is independent of post-write Flash verification.

## Measured throughput and one-time chip authorization

The shared CLI now sends `elapsed_ms` and `bytes_per_second` with the
`program` and `verify` progress events. These are **actual average
rates since that phase began**, measured with a monotonic clock in the
CLI. Repeated Verify passes are included in one cumulative Verify
timer. The Web v2 client displays `bytes_per_second / 1024` as
`KiB/s` next to byte progress. Erase progress is in sectors; no
fictional erase byte rate is displayed.

The previously observed Write speed was about **32 KiB in 6 s**.
Investigation found that each 512-byte chunk previously opened
`/dev/slot-md` separately and caused a fresh kernel CFI query plus
AMD autoselect. For a 4 MiB image, that was **8192 redundant
identifications**. The MegaDrive Flash adapter now holds a single
FD throughout Identify, Erase, Write and Verify; the kernel binds
successful identity authorization to that FD and invalidates it on
close or any observed cartridge-detect change. Every destructive
ioctl still checks the native MD slot, 5V connector setting, current
presence and incompatible bus modes. New kernel and MD core binaries
**must be installed together** for Flash writes to continue working.

This is a private experimental behavior change without changing the
existing ioctl numbers or payload structs. The kernel and MD core
are not independently compatible across the old and new session
implementations. Neither full hardware throughput nor new cartridge
handling has yet been measured.

## Erase performance and EBUSY diagnostics

The kernel now polls for sector-erase completion but **does not** read
all 32,768 words in a 64 KiB sector a second time before continuing.
That per-sector full scan caused needless GPIO reads and significant
latency. If Verify is selected, the shared CLI performs full image
readback after all programmed sectors and repeats the entire image
comparison for each selected pass. Programming still polls individual
Flash words to ensure the device is ready for the next command.

A previous physical test stopped after 33/57 sectors with kernel
`ioctl 0x800c52f2 failed (-16)` (`EBUSY`), **not** a DQ6/DQ5
erase-completion or verification failure. The kernel now waits for
short `select_lock` contention, and logs which incompatible bus mode
or other occupied slot causes a persistent EBUSY. The observed
`3300mV` after the failed CLI operation is consistent with the
hardware scope restoring the prior slot voltage; it does not prove
the Flash was erased at 3.3V.

Cancellation is supported at safe chunk/sector boundaries. An
in-progress physical erase/program pulse is allowed to finish before
stopping, and the driver restores array mode. Interrupted or failed
writes are reported as failed, never successful.

## Tests and release

The current branch has compiler and mocked CLI tests for the actual
Flash adapter, source-file and geometry validation, phase progress,
wrong chip identity, erase failure, complete readback and mismatch.
These tests cannot replace physical tests.

**Confirmed on hardware (2026-11-23):** EN29LV320CB on the Retrode3
responded to CFI QRY with 4MiB/71-sector geometry and returned to ROM
mode (header `SEGA GENESIS` at 0x100).

**Confirmed subsequently on hardware (2026-11-23):** one complete
3.13 MiB ROM was erased, programmed and byte-for-byte verified
(1 pass) on the Retrode3, and the resulting cartridge worked in
an original MegaDrive console. This validates the **previous**
kernel/Flash implementation, not the newly introduced per-FD
authorization or measured-speed code.

**Still requiring real-device tests:** new per-FD identity lifecycle,
new writing throughput, media-removal handling, interrupts and
repeated Verify passes. The feature branch remains separate from
`develop`/`master` pending those tests.
