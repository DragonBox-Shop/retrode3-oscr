# SMS over the Retrode2 hardware plugin: Stage 0 (no cartridge I/O)

This is **only** read-only **metadata parsing** groundwork, NOT an enabled
Retrode3 core. Do not use the normal MD ROM mode to access an SMS cart.

## Architecture

- Planned core ID: `sms-md`; logical system `sms`, **physical slot `md`**.
  Never assume the logical system implies the physical slot.
- Use the common CLI and generic ABI v3 for future discovery, layouts,
  selection, CRC, progress, cancellation, and atomic output.
- `sms_header_v1.h` parses a *supplied* 16-byte `TMR SEGA` metadata
  window; it does not touch the bus and its declared size is **unverified**.
- Initial work does not install/enable a `.so`, alter the kernel or frontend,
  or expose SRAM/mapper writes.

## Original OSCR behavior

The legacy `Cart_Reader/SMS.ino` has a specific `adapter_retrode` route.
It sets **5 V**, reads **D0..D7** with dedicated **RD/OE** control, and
immediately issues `$FFFC=0`, `$FFFD=0`, `$FFFE=1`, `$FFFF=2`
on setup. The `TMR SEGA` header is commonly at CPU `$7FF0..$7FFF`;
the size nibble at `$7FFF`. Bank reads above 32 KiB write `$FFFF`
and read 16 KiB at `$8000`. The upstream mirror-detection algorithm
also writes mapper registers and has known false size detections.
Do not execute this setup on an unreviewed Retrode3 bus.

The current `retrode3-kernel` `project-retrode3-v2` DT configures
`md` as a **16-bit bus**; `MD_ROM` aligned reads use `read_word()`,
other reads choose a byte lane by address parity. This is not a
verified replacement for the SMS 8-bit RD/OE sequence.

## Mandatory hardware gate

Before any physical access, verify the actual Retrode2 adapter's electrical
pinout against Retrode3 PCB revision 2.9.5 and the deployed kernel: VCC
(5 V), ground, level shifting, connector orientation, address/data lane,
CS/OE/RD/WR, reset and timing. An inserted-adapter sense signal alone
is not proof of safe access. Do not hot-plug during testing.

Implement a reviewed typed **SMS-on-MD HAL** before probing. It must
select the proper bus lane and line strobes without uncontrolled writes.
No raw physical addresses, voltage or register-write values in UI overrides.
No SRAM/save/flash access until separately approved.

## Release steps

1. **This stage:** C++11 header parser + deterministic mock tests.
2. Validate adapter/PCB pinout and new kernel HAL using mocks and controlled
   hardware tests before any live cartridge read.
3. Bounded no-write `TMR SEGA` probe at applicable candidate locations
   (not necessarily always `$7FF0`); fail closed if identity/layout is
   ambiguous or metadata absent.
4. Only after approval, initially read known linear ROMs; then add
   banked Sanni-derived sequences and independent full-ROM CRC verification.
   SMS header size may be smaller than the physical ROM; no blind guesses.
5. Add the separate `sms-md.so` / `sms-md.core` and ABI v3 to the
   common host, preserving existing SNES/MD/NES regressions. Implement
   web support only via the generic capabilities/selection payload.

Different adapters and Game Gear / SG-1000 / Mark III are distinct cases,
not automatically enabled by this SMS core. A header checksum alone is
not a verified ROM identity.

For a change touching native code or the Makefile, run
`./tools/preflight-ci.sh` with the pinned submodule before merge. When
only GitHub connector access exists, keep the work in an isolated PR
against `develop`, then perform the required checks before merge to
`develop`, physical acceptance before `master`.
