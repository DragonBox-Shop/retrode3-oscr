# Retrode3 native OSCR SNES binary: implemented NDJSON v1 protocol

**Implementation snapshot:** 2026-11-17, branch `migration/oscr-firmware`,
commit `b5dd1b2bb123d3845faba4d56d701ce28e93826d`.
This document describes **what the running executable implements**, not a
future web API or generic protocol that may be added later.

Implementation:
- `src/retrode3/oscr_snes_native.cpp`
- `src/retrode3/ndjson_protocol.cpp`
- `include/retrode3/ndjson_protocol.h`
- `tests/test_native_snes.py`

Hardware-confirmed by a physical Terranigma (Germany) (Rev 1) cartridge:
streaming HiROM read of 4,194,304 bytes, progress events through 100%,
byte-identical dump versus Python reference (CRC32 `6FB8A4DF`);
interactive `ready`, `probe`, `get_capabilities`, `read_rom`, and
`cancel` with error code `cancelled` were observed. A test run's partial
file cleanup and exact bytes should still be verified after cancellation
on that hardware. Native **SNES only**, standard LoROM / HiROM only.

## Roles and transport

Browser <--HTTP/SSE or WebSocket (future backend bridge)--> Retrode3
web backend <--stdin/stdout NDJSON v1--> `oscr-snes-native`
--> original pinned OSCR SNES C++ archive --> Retrode3 kernel bus.

The browser must **never** execute shell commands or open a cartridge
device directly. The web backend owns the process, command validation,
paths, authorisation, job IDs, client reconnection and slot exclusivity.
**There is no HTTP/SSE server or cartridge-insertion watcher in this
binary.** Implement these in the backend.

Build:
```sh
git switch migration/oscr-firmware
git pull --ff-only
make -f Makefile.migration native-snes
make -f Makefile.migration test-native-snes
```

Interactive process:
```sh
build/migration/oscr-snes-native --protocol ndjson-v1 --interactive
```

- `stdout`: exactly one JSON event per line, flushed immediately.
  Read as a *stream of lines*, not as one JSON document.
- `stdin`: exactly one JSON command object per line, terminated by LF.
  Keep stdin open while interactive; EOF exits normally.
- `stderr`: operational diagnostics; don't parse it as protocol events.
- All example objects below are **separate lines**, not an array.
- `request_id` is a caller-provided correlation key for each command
  (1–64 ASCII letters/digits/`_`/`-`). Reuse it for a matching
  `cancel`. In one-shot mode the ID is `cli`.
- Errors for particular interactive commands are delivered as `error`
  events; an interactive process can remain alive and exit with code 0
  despite individual job errors. Handle events, not just process status.

### Commands accepted now

```json
{"command":"get_capabilities","request_id":"cap1"}
{"command":"probe","request_id":"probe1"}
{"command":"read_rom","request_id":"rom1","output":"/srv/retrode3/roms/job-123.sfc"}
{"command":"read_rom","request_id":"rom2","output":"/srv/retrode3/roms/job-124.sfc","mapping":"lo","size_bytes":524288}
{"command":"cancel","request_id":"rom2"}
```

Fields and constraints:
- `command` and `request_id` are required strings.
- `read_rom` additionally requires `output`, a path string (maximum
  1024 bytes in the current C++ string representation, no CR/LF).
- `mapping` is optional: `"lo"` or `"hi"`.
- `size_bytes` is optional: positive integer 131072–4194304, also
  divisible by 32768 for LoROM or 65536 for HiROM. The binary probes the
  cartridge first even when overrides are supplied.
- The other three commands do not accept `output`, `mapping`, or
  `size_bytes`.
- Unknown fields, duplicate fields, invalid types, and extra commands
  are rejected. Current lightweight parser does **not** support JSON
  `\\u` escapes, floats, booleans or null in commands. Send ordinary
  ASCII request IDs and JSON strings.
- Current parser's accepted command line limit is 8192 bytes.
- `cancel` takes the *running* read's request ID, **not a new ID**.
  Cancellation is checked between OSCR file-write blocks. It may not
  interrupt a stalled kernel syscall immediately.

### Events returned now

The normal event prefix is
`{"protocol":"retrode3-ndjson","version":1,"event":"...", "request_id":"..."}`,
except where documented below.

**ready**: emitted once when interactive binary starts. No request_id.

```json
{"protocol":"retrode3-ndjson","version":1,"event":"ready","system":"snes"}
```

**capabilities**: response to `get_capabilities`, no cartridge access.

```json
{"protocol":"retrode3-ndjson","version":1,"event":"capabilities","request_id":"cap1","system":"snes","core":"oscr-snes-cpp","commands":["probe","read_rom","cancel","get_capabilities"],"mappings":["lo","hi"],"size_override":true,"candidate_selection":false,"database_lookup":false,"write_operations":false,"max_rom_bytes":4194304}
```

**probe_result**: result of original OSCR C++ `checkcart()`.

```json
{"protocol":"retrode3-ndjson","version":1,"event":"probe_result","request_id":"probe1","system":"snes","engine":"OSCR C++ checkcart","mapping":"hi","size_bytes":4194304,"chips":2,"checksum":"3856","title":"TERRANIGMA D","candidates":[],"candidate_lookup":"not_integrated"}
```

Note: `candidates:[]` does **not** mean zero matching games. The OSCR
cartridge database is **not integrated** into the native binary yet.
Display detected header values and mark game database matching as
unavailable. No `input_required` or `select` command is implemented.

**progress**: while `read_rom` is running.

```json
{"protocol":"retrode3-ndjson","version":1,"event":"progress","request_id":"rom1","phase":"read_rom","bytes_done":41984,"bytes_total":4194304,"percent":1}
```

Reported `bytes_done` is successfully written output bytes. Events
start at 0, advance roughly in 1%-sized steps, and end at 100% after
successful publication. Render `bytes_done / bytes_total`, not a
timer or guessed progress. An error/cancel has **no terminal 100%**.
The `percent` is integer and is capped at 99 until publication.

**result**: finished read; `verified:null` is intentional because the
native OSCR database CRC verification is not implemented.

```json
{"protocol":"retrode3-ndjson","version":1,"event":"result","request_id":"rom1","ok":true,"engine":"OSCR C++ bank readers","mapping":"hi","bytes_written":4194304,"output":"/srv/retrode3/roms/job-123.sfc","verified":null}
```

**error** (job operation, cancel or protocol failure):

```json
{"protocol":"retrode3-ndjson","version":1,"event":"error","request_id":"rom2","code":"cancelled","message":"cancelled"}
```

Current error codes:
- `cancelled`: current read cancelled, temporary file removed through
  the normal exception/cleanup path.
- `operation_failed`: probe/read error (message is for diagnostics).
- `no_active_job`: a cancel command arrived while idle.
- `protocol_failure`: fatal session input failure (request_id `session`).
- `invalid_command`, `busy`: these two are produced by the input
  parser/read loop and may **omit** the normal
  `protocol`/`version` prefix and sometimes `request_id`.
  Correlate with a job only when a valid request ID is present.
  Do not assume all error events share identical fields.

Error messages should not be shown verbatim as user-friendly web UI text;
translate `code` to localised messages in the backend/frontend.

## Legacy and one-shot CLI compatibility

These old commands still work:
```sh
build/migration/oscr-snes-native --action probe
build/migration/oscr-snes-native --action read-rom --output /tmp/test.sfc
```

Without `--protocol` the runner prints **one final legacy JSON object** on
success and a legacy `{"ok":false,"error":"..."}` to stderr on failure;
there are **no progress events**. With `--protocol ndjson-v1`
but without `--interactive`, `--action probe|read-rom` instead emits
events on stdout (with request_id `cli`), then exits.

The output file is created first under a same-directory
`<output>.partial-XXXXXX` temporary name and published with
no-overwrite semantics. Existing files cannot be replaced.
Normal exception paths remove the partial file; power loss / SIGKILL
might leave partials, so the backend should clean stale files safely.

## Suggested backend state machine (NOT IMPLEMENTED HERE)

1. Own one serialised hardware job per shared cartridge bus (or stricter
   exclusion until electrical concurrency is verified).
2. Launch the binary using a **fixed argument array**, no shell string;
   prefer a service account with minimal permissions.
3. Wait for `ready`; send `get_capabilities` and `probe` as commands.
4. On `probe_result`, display title, mapping, detected size; no database
   candidates yet. Offer explicit allowed overrides only.
5. Generate a destination path within a private, writable, safe directory
   from server-side job IDs. **Never** pass a browser-supplied arbitrary
   filesystem path through `output`. The current native binary's path
   validation does not prevent traversal or writing into arbitrary
   writable directories.
6. Start `read_rom`, forward `progress` as server-sent events (SSE).
   When browser requests cancellation, send matching `cancel` line.
7. On `result`, mark file complete. On `error`, mark failed/cancelled.
   Handle stdout EOF and process death as separate failure conditions.
8. Do not auto-switch voltages or issue unsafe bus probes. Plug-in ID,
   automatic insert detection and safe voltage profiles are separate
   kernel/hardware tasks, not a binary API feature today.

Not yet supported by this v1 SNES stage: auto-insert events, ROM database
matching, alternative ROM sizes from CRDB, game candidate selection,
`input_required`, `select`, NES / MD / GB / GBA / N64 cores, SRAM,
flash writes, generic plugin registry, HTTP endpoints, SSE server or
WebSocket server. Keep frontend capability-driven and provide a clear
"unsupported" state rather than faking these.

## Smoke test and verification

```sh
make -f Makefile.migration test-native-snes
build/migration/oscr-snes-native --protocol ndjson-v1 --action probe
build/migration/oscr-snes-native --protocol ndjson-v1 --action read-rom \
  --output /tmp/retrode3-web-protocol-test.sfc
```

For Terranigma Germany Rev 1 on real hardware, `cmp` against the known
reference returned identical bytes and CRC32 `6FB8A4DF`.
The user also verified interactive cancellation returns
`{"event":"error","code":"cancelled",...}` with matching request_id.

**Source of truth:** if this document and compiled binary ever diverge,
tests and source behavior win; update this document in the same feature
commit when you change the protocol.
