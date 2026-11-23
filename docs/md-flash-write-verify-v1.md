# Flash write and verify: shared CLI / plugin contract (proposal v1)

Status: **experimental test-build contract**. Erase, program and verify
MUST be exposed together as soon as their functional paths are implemented,
so the operator can run actual hardware acceptance tests. No additional
\"already hardware approved\" gate may prevent the first tests. The first
MegaDrive CFI query (AMD/Fujitsu command set 0x0002, 4 MiB geometry) and
return-to-ROM readback were verified on a real Retrode3 on 2026-11-23.
The separate kernel experiment is still a Draft PR. Manufacturer/device
identification and safe sector erase/program are not yet hardware accepted.

## Current implementation status (2026-11-23)

In this **Draft experimental branch**, the optional C ABI is implemented
in `include/retrode3/core_flash_v1.h`, with a MegaDrive transport adapter
`src/retrode3/oscr_md_flash_lab.cpp` and the separate target
`make -f Makefile.migration native-md-flash-lab` yielding
`build/migration/md-flash-lab/md.so`. The regular
`build/migration/md.so` remains **read-only**.

The shared `retrode3-cli` implements `probe_flash`, `write_flash` and
`verify_flash` in its existing NDJSON host and one-shot CLI.
For **operator testing only** on an isolated, root-run test install with
an experimental Retrode3 kernel and an expendable 4MiB Eon cartridge:

```sh
# Once matching test kernel and Flash-enabled test .so are installed:
retrode3-cli --protocol ndjson-v1 --core md --action probe-flash
retrode3-cli --protocol ndjson-v1 --core md --action verify-flash \
  --source /usr/local/games/ROMs/MD/known-good.md
retrode3-cli --protocol ndjson-v1 --core md --action write-flash \
  --source /usr/local/games/ROMs/MD/known-good.md --confirm-erase
```

The host owns the bus lock, scoped 5V profile, kernel test-only gates,
progress and byte-for-byte verify. **Do not run a second independent
`flock` around the common CLI invocation**, which already acquires it.
An **administrator** must run this lab flow; no generic Web root access,
no auto-probe writes, and no production installer are included here.
The narrow privileged Web bridge and tested Web Write Cart page remain
pending. MIPS build and real erase/program verification remain pending.
The kernel test-ioctl implementation is tracked separately in
`DragonBox-Shop/retrode3-kernel#7`.

## Architecture

- One shared `retrode3-cli` owns NDJSON framing, request IDs, bus locking,
  progress, cancellation, result validation and verification summaries.
  Cartridge-specific operations live in optional versioned core capabilities.
  The browser must not know MD chip-specific commands or erase layouts.
- A Flash-capable core must advertise **independent** capabilities
  `probe_flash`, `write_flash` and `verify_flash` in the experimental
  build as soon as the corresponding implementations exist. The core
  must NOT require prior successful physical hardware testing just to
  expose them to the test operator. Ordinary production builds may
  withhold experimental capabilities until acceptance. Do not change
  the existing `probe` / `read_rom` ABI or re-purpose ROM-database
  `verification` as physical Flash verification.
- Flash probing includes a command write. It is **explicit user action
  only**, never part of automatic insertion detection.
  `sense=active` means occupied, not approved for Flash access.
- The shared CLI mediates trusted, bounded source-file access; a browser
  may select a file from the approved ROM library but must never send
  arbitrary device paths, raw addresses, raw write commands or
  caller-selected voltage. A Flash write requires positively matching
  physical-slot identity, approved chip ID, CFI geometry, board/voltage
  profile, filename/length and a user confirmation. These checks are
  repeated immediately before the first destructive command.

## User workflow

1. In **Write Cartridge** choose an occupied physical slot and explicitly
   request **Detect Flash**. Display chip manufacturer/device ID, size,
   geometry and whether this exact hardware combination supports writing.
   If identification is missing/ambiguous, disable **Write** and **Verify**.
2. Select a ROM file from the trusted shared file library. Reject an image
   larger than Flash capacity, unsupported mapping and incompatible sector
   boundaries. Show a confirmation with image size, target and that
   existing Flash contents will be erased.
3. **Write Cartridge** runs the phases `erase`, `program`, `verify`.
   Verify is **enabled by default**, and success requires a complete
   byte-for-byte readback comparison to the selected source file.
   The user cannot silently interpret completion of `program` as
   successful verification.
4. **Verify Cartridge** is also offered as a **separate read-only job**
   for a selected ROM file and a positively identified Flash cartridge.
   It makes no erase/program commands, compares precisely the file's
   bytes to the corresponding Flash range and reports a mismatch
   with its first differing byte offset. Flash capacity can exceed ROM
   length; untouched tail bytes are not part of this verification unless
   the selected layout explicitly says otherwise.
5. Any mismatch, unexpected ID/geometry, short read, communication error,
   cartridge removal or incomplete job fails closed. Never report
   `ok:true` merely because all bytes were transmitted to Flash.

## Progress in existing NDJSON v1 envelope

The shared host emits progress in the same event stream as `read_rom`.
A new progress phase may reset `percent` to zero; the UI MUST label the
phase instead of pretending this is a single monotonic percentage.

```json
{"protocol":"retrode3-ndjson","version":1,"event":"progress","request_id":"job1","phase":"erase","units":"sectors","units_done":3,"units_total":71,"percent":4}
{"protocol":"retrode3-ndjson","version":1,"event":"progress","request_id":"job1","phase":"program","units":"bytes","bytes_done":65536,"bytes_total":4194304,"percent":1}
{"protocol":"retrode3-ndjson","version":1,"event":"progress","request_id":"job1","phase":"verify","units":"bytes","bytes_done":65536,"bytes_total":4194304,"percent":1}
```

- Each phase is reported from **completed actual work**, never elapsed
  time or a speculative estimate. In `erase`, count only sectors whose
  completion/polling passed. In `program`, count only confirmed programmed
  bytes. In `verify`, count only read-back bytes already compared to
  the source. For `read_rom` preserve the current `bytes_done`,
  `bytes_total`, `phase` and `percent` contract.
- A normal finished phase may reach 100%, but the **whole job** succeeds
  only after mandatory verification if chosen. No terminal success event
  for an erased, partially written, failed or cancelled cartridge.
- A job `result` may carry
  `flash_verification:{"status":"verified","method":"byte_compare","bytes_compared":4194304,"first_mismatch_offset":null}`.
  For mismatches the job fails and reports
  `flash_verification.status="mismatch"` with the first mismatch
  offset. The older `verification` object in `read_rom` refers to a
  **ROM database match**, not a post-write cartridge comparison.
  Make this distinction explicit in the UI.
- A standalone `verify_flash` job emits only the `verify` phase.
  It never sets `write_operations:true` or enables a write button
  unless write support is separately hardware accepted.

## Cancellation and hardware safety

- The host keeps the exclusive `/run/lock/retrode3/bus.lock` and
  kernel selection lock for an operation. Normal reader and Flash jobs
  cannot interleave; a system event or browser refresh must not trigger
  Flash commands.
- Cancellation between sectors/chunks may stop starting new work, but
  must not interrupt an in-flight erase/program pulse or polling needed
  to bring the chip to a known state. Reset the chip to array/read mode
  and release the data GPIOs before returning. The UI must distinguish
  `cancelling` (waiting for safe point) from `cancelled`.
- If Flash reports power, bus or poll timeout errors, abort and mark
  the cart as possibly partially programmed. No automatic resume, retry,
  fallback to another voltage or guessed manufacturer command.
- The existing passive ROM reader, NES/SNES/SMS adapters and SRAM paths
  must remain unchanged. Experimental `write_flash` and `verify_flash`
  MUST be callable **before** the first physical tests, otherwise those
  tests cannot occur. Keep the scope limited to a positively identified,
  supported cartridge, an explicit user confirmation, an expendable test
  cart, an experimental kernel-module gate and exclusive bus ownership.
  Do not ship the same experimental privilege to production by default.

## Acceptance matrix before exposing Write Cartridge

- Mock tests: no write capability before positive Flash ID, invalid
  source lengths rejected, phase counters monotonic within each phase,
  cancellation and progress correlated to one request ID, no 100%
  success on error, and mismatch reports exact first differing offset.
- Real Retrode3: correct MD 5 V at connector and regulated Flash VCC;
  manufacturer/device ID + CFI geometry; /CE /OE /WE timing and output
  direction; successful full erase, full programming, independent
  byte-for-byte readback, CRC summary, post-command reset and
  re-insertion readback. Test interruption/recovery on a disposable
  cart. **Expose test-build operations before these tests**, not after.
  Only after passing them remove the experimental label and release to
  production. Web v2's Write Cartridge screen may invoke the test-only
  flow from the outset; never treat the test workflow as customer-ready.
- Run `./tools/preflight-ci.sh` in the full checkout before ABI, CLI,
  core or test changes are pushed. Leave any implementation that has
  only GitHub-connector review on a short-lived Draft PR branch until
  those tests and real hardware acceptance are complete.
