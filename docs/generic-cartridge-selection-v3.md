# Generic Cartridge Selection / Override Contract (NDJSON v1 + core ABI v3)

**Status:** Development contract. Applicable to every new cartridge adapter. Host NDJSON envelope remains `{"protocol":"retrode3-ndjson","version":1,...}`; `selection.version` and `schema_version` independently version the selection vocabulary. This is **not** a command to write cartridge memory.

## Architecture and compatibility

- Optional `retrode3_core_get_operations_v3()` exposes four callbacks in `include/retrode3/core_operations_v3.h`. A core may retain its mandatory v1 ABI and optional NES v2 ABI. Legacy v1 and existing v2 commands stay compatible.
- The Sanni-derived core stays unchanged. The Retrode3-owned adapter implements v3, converting typed generic parameters into reviewed core-specific read operations.
- A root-owned packaged JSON sidecar is recommended **only for static UI metadata** (labels, fields, dependencies, per-adapter physical slot). Runtime support, bank-switching eligibility and cartridge safety are decided by the executable adapter's `plan_read` and `read_rom` callbacks. A database entry or editable JSON must **never** enable an unsupported mapper or write.
- `slot` in the existing `.core` manifest MUST match the descriptor's physical slot and controls the physical bus selection. Different adapter routes are different IDs, e.g. `sms-snes` (`slot=snes`) and `sms-md` (`slot=md`), even when the shared upstream logic is identical. The backend never guesses the slot from the console name. A `sense` signal is occupancy, not authorization for a voltage or access.
- `capabilities_json` is metadata-only and can run without touching the cartridge. `fast_probe_json`, `plan_read`, `read_rom` run while holding the host bus lock.
- New adapters should not require common host changes or frontend-specific data. The host uses fixed generic fields while the adapter owns the semantics of parameter IDs.

## Commands

The existing `probe`, `get_capabilities`, `read_rom` actions stay. For v3 reads, `selection` is a **single object**; passing old `mapper`, `layout_id`, `mapping` and `size_bytes` alongside it is an error. Missing selection means `auto` only. Unknown fields and mismatched versions must be rejected.

Automatic:

```json
{"command":"read_rom","core":"nes","request_id":"read1","output":"/tmp/game.nes","selection":{"version":1,"mode":"auto","schema_id":"nes-layout-v1"}}
```

Candidate selection (title and configuration coupled):

```json
{"command":"read_rom","core":"nes","request_id":"read2","output":"/tmp/game.nes","selection":{"version":1,"mode":"candidate","schema_id":"nes-layout-v1","candidate_id":"mmc1-0042"}}
```

Layout selection (only when explicitly offered by probe):

```json
{"command":"read_rom","core":"nes","request_id":"read3","output":"/tmp/game.nes","selection":{"version":1,"mode":"layout","schema_id":"nes-layout-v1","layout_id":"m1-128-0"}}
```

Explicit manual override (independent of recognition):

```json
{"command":"read_rom","core":"nes","request_id":"read4","output":"/tmp/game.nes","selection":{"version":1,"mode":"override","schema_id":"nes-layout-v1","parameters":{"mapper":1,"submapper":0,"prg_kib":128,"chr_kib":0,"mirroring":"horizontal","battery":false}}}
```

`mode=auto` MUST NOT carry candidate IDs, layout IDs or parameters; `candidate` requires exactly `candidate_id`; `layout` requires exactly `layout_id`; `override` requires nonempty `parameters` and neither ID. No mode inference from incidental fields. The schema revision must match the active core. The host limits parameter count, key length and types; the core must reject unknown keys, unsupported combinations, omitted required values and any unapproved writes. **No host-controlled voltage, address or raw register-write parameter.**

## `get_capabilities` response

```json
{"protocol":"retrode3-ndjson","version":1,"event":"capabilities","request_id":"cap1","core":"nes","system":"nes","slot":"nes","selection_schema":{"schema_version":1,"schema_id":"nes-layout-v1","selection_modes":["auto","candidate","layout","override"],"fields":[{"id":"mapper","label":"Mapper","type":"enum","required":true,"values":[{"value":0,"label":"NROM"},{"value":1,"label":"MMC1"},{"value":4,"label":"MMC3","read_supported":false}]},{"id":"prg_kib","label":"PRG ROM (KiB)","type":"integer","required":true,"minimum":16,"maximum":512},{"id":"chr_kib","label":"CHR ROM (KiB)","type":"enum","values":[0,8,16,32,64,128]},{"id":"battery","label":"Battery-backed RAM","type":"boolean","default":false},{"id":"mirroring","label":"Mirroring","type":"enum","values":["horizontal","vertical","four_screen"]}],"rules":[{"when":{"mapper":1},"allow":{"prg_kib":[32,64,128,256,512]},"read_supported":true},{"when":{"mapper":1,"battery":true},"read_supported":false,"reason":"SRAM-safe MMC1 pulses not approved"}]}}
```

`read_supported` in schema is a **display hint**, not an authorization. Dependencies are declarative conditions, NOT executable code. A core may add arbitrarily named fields or richer dependent rules without modifying the common host. The interface only needs generic types `enum`, `integer`, `boolean` (and optional `string`) and generic visibility/required conditions.

## `probe` response

```json
{"protocol":"retrode3-ndjson","version":1,"event":"probe_result","request_id":"probe1","core":"nes","system":"nes","selection_schema_id":"nes-layout-v1","identification_status":"ambiguous","ambiguity":"titles_only","selection_required":false,"candidates":[{"id":"nrom-0001","title":"Game (EU)","layout_id":"nrom-32-8","read_supported":true},{"id":"nrom-0002","title":"Game (US)","layout_id":"nrom-32-8","read_supported":true}],"read_layouts":[{"id":"nrom-32-8","label":"NROM 32/8 KiB","read_supported":true}]}
```

When `ambiguity:"layouts"`, `selection_required:true` and at least two distinct viable layouts exist; `read_rom` in auto mode MUST reject. If only titles differ, automatic layout reading may proceed, but the UI should still offer an optional game-title dropdown. `unknown` has no candidates and manual override remains available. Unreadable/blocked candidates are returned with `read_supported:false` plus `reason` and cannot be selected. Stable candidate IDs are scoped to the catalogue/schema revision, never the visible list index.

Probes MUST be bounded, read-only and must not dump an entire ROM just to populate the menu; in particular NES v2 samples only 1024 PRG bytes. Fingerprints are suggestions, never CRC verification.

## `result` response

```json
{"protocol":"retrode3-ndjson","version":1,"event":"result","request_id":"read4","ok":true,"system":"nes","output":"/tmp/game.nes","bytes_written":131088,"crc32":"B78FDA39","selection_mode":"override","read_configuration":{"mapper":1,"submapper":0,"prg_kib":128,"chr_kib":0},"identity":{"selected_candidate_id":null,"matched_candidate_id":"mmc1-0042","title":"Rad Racer (PAL)"},"verification":{"status":"verified","verified":true,"method":"payload_crc32","expected":"654F4E90","actual":"654F4E90"}}
```

Valid verification statuses: `verified`, `mismatch`, `not_found`, `ambiguous`, `not_checked`. Only an unambiguous reference match can set `verified:true`. Successful reading of an unknown cartridge is `not_found`, not `verified`. A selected record whose CRC differs is `mismatch`; include its expected and observed CRC. A v1 core with no DB verification reports `verified:null` for compatibility.

The host owns output atomicity, output checksum and cancellation; the adapter returns the true applied read configuration and verification data. Errors do not publish a partial ROM. Cancellation is reported separately from verification.

## ABI v3 implementation requirements

The adapter's `capabilities_json` and `fast_probe_json` callbacks return **JSON objects** (without NDJSON envelope); the host validates their structure and wraps them in an NDJSON event. `plan_read(selection)` must reject unsafe parameters and return exact streamed payload size and optional header-prefix size; it must not perform cartridge writes. `read_rom(selection)` revalidates and streams exactly the planned payload through the v1 host IO callback. It supplies the final prefix bytes and a JSON object containing `read_configuration`, `identity`, `verification`. The host checks the size and optional payload CRC, patches only its temporary output file, then publishes atomically. The core's callbacks must not write any cartridge RAM/save/flash or alter power levels from user-supplied values.

No unreviewed promotion to `master`. Run host protocol/schema tests and SNES/MD/NES regression tests, then the physical cartridge/adapter acceptance matrix for every newly enabled bus operation.
