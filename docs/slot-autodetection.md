# Slot-wide automatic system detection (development only)

Status: **mock-CI tested**, not yet accepted on physical SMS-0007 hardware.
This document accompanies the draft PR against `develop`.

## Shared CLI protocol

```sh
/usr/bin/retrode3-cli --protocol ndjson-v1 --action probe-slot --slot md
/usr/bin/retrode3-cli --protocol ndjson-v1 --interactive
```

Interactive request (one JSON object per line):

```json
{"command":"probe_slot","slot":"md","request_id":"slot-1"}
```

The host enumerates trusted, enabled and ABI-compatible v3 modules whose
descriptor/manifest physical `slot` matches the given slot. Probes are
serialized under one `/run/lock/retrode3/bus.lock`; enabled core IDs are
visited in lexical order. A core must explicitly report
`system_detected:true` in its validated v3 probe response for a match.
The `identification_status` field concerns game identity, **not** the
console/adapter identity. A game title or matching ROM layout alone is
never proof of a physical system.

Example response shape:

```json
{"protocol":"retrode3-ndjson","version":1,"event":"slot_probe_result","request_id":"slot-1","slot":"md","detection":"detected","core":"sms-md","candidates":["sms-md"],"attempts":[{"core":"md","outcome":"failed","reason":"not a standard SEGA MegaDrive header"},{"core":"sms-md","outcome":"matched"}],"voltage_status":"manual_required","probe_result":{"protocol":"retrode3-ndjson","version":1,"event":"probe_result","request_id":"slot-1","core":"sms-md","system":"sms-md","slot":"md","identification_status":"unknown","system_detected":true}}
```

`detection` is `detected`, `manual_required`, `empty`, or
`unavailable`. An absent or inconclusive header, unsupported probe
voltage, or failing probe must not select an adapter. A positive match
short-circuits subsequent probes; the `attempts` array provides
diagnostics. `probe_result` contains the selected core's v3 payload
(including candidate/layout metadata). Existing per-core `probe`,
`get_capabilities` and `read_rom` commands remain unchanged.

## Voltage restrictions

The multi-core scan only performs bounded, read-only probes at a
**confirmed 3300 mV**. New adapters must explicitly expose
`probe_voltage_mv:3300` in their v3 capability schema. Unknown,
unavailable or 5000 mV slot states do not trigger speculative reads.

Once a valid MD header has been found, the MD adapter declares the
reviewed `read_voltage_mv:5000` and
`automatic_voltage_switch:true` policy. The host then attempts to
write `5000` through the kernel's per-slot `vcc` sysfs control,
while holding the same bus lock, and reads back the resulting voltage.
Lack of write permission yields `voltage_status:"manual_required"`,
not a falsely claimed successful transition. No generic privilege
escalation is introduced.

**SMS-0007 remains experimental** and deliberately lacks the automatic
switch flag. Its successful 3.3 V header probe reports system identity
but `voltage_status:"manual_required"`. Full 5 V SMS ROM reading still
requires operator opt-in, confirmed hardware safety and the original
core's voltage gate. This is not a cartridge power-off mechanism.

Do not automatically switch any unknown adapter to 5 V. Slot sense
must be active before probing and before/after voltage changes. Removal
invalidates the cached Web result. Kernel and mechanical hot-swap timing
must be physically evaluated before general release.

## Web v2 integration

The Web backend handles multi-core slots using one `probe_slot`
command through its existing JobManager, not independent ad-hoc
core readers. Single-core slot behavior and manual selection remain.
A failed/inconclusive automatic identification exposes
`choose_reader` and leaves voltage unchanged. The browser does not
issue hardware operations while refreshing.

## Acceptance

Run `./tools/preflight-ci.sh` in a full checkout, plus
`make -f Makefile.migration test-slot-autoprobe` and all native
regressions. A successful CI mock is NOT a physical electrical acceptance.
On a real Retrode3 test MD and SMS headers, unknown carts, removal during
probe, safe 3.3 V starting state, permitted/denied 5 V transition,
reboot and web manual fallback before merging to `develop`.
