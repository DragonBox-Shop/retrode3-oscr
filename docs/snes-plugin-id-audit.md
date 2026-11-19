# SNES-slot adapter identification: compatibility audit (ongoing)

Date: 2026-11-17. **No SNES pin is reserved for ID yet.**
This is a design note for Retrode3 3.0, not a finished specification.

## Two separate connector families

- **Legacy Retrode2 adapters** (GBx, N64, SMS) plug into the Retrode3
  **MegaDrive** slot. Their previous presence signal is MD A32, and
  they have no unique adapter ID. Keep working via safe software probes.
- **Sanni/OSCR expansion adapters** generally plug into the reader's
  **SNES cartridge slot** (62 contacts, suitable PCB thickness 1.2 mm);
  some adapters use MegaDrive or additional hookups. The modern OSCR
  HW5 mainboard's 36+38-pin **internal board-to-board backplane**
  is a *different connector* from the 62-contact external SNES cartridge
  slot. Do not transfer backplane pin numbers to SNES edge contacts.

Sources:
- `sanni/cartreader` discussion #354:
  https://github.com/sanni/cartreader/discussions/354
- `sanni/cartreader` hardware repository:
  https://github.com/sanni/cartreader/tree/master/hardware
- `sanni/cartreader` wiki supported adapters and hardware build:
  https://github.com/sanni/cartreader/wiki
- Retrode3 3.0 uploaded source:
  `Retrode3-3.0-Case-Design-sch.pdf`, sheet 4/9, SNES slot
  `GS-D-062C-LF`. The PDF is attached in the project discussion,
  **not** committed to this repository.

## What is confirmed

The Retrode3 3.0 SNES slot schematic depicts a number of physically
present contacts as no-connect (outer SNES expansion/audio contacts).
Those contacts could be candidates **only if existing SNES cartridges
and adapters do not rely on them**. They are not globally free simply
because one current Retrode3 PCB does not connect them.

A concrete conflict is documented in the Sanni community's Atari
Jaguar adapter instructions: they describe using the
**SNES left-audio contact, pin 31**, as an optional additional
signal routed from another OSCR port. So SNES pin 31 is **NOT** a
universally available ID pin.
Source: https://github.com/sanni/cartreader/discussions/354

Other community adapters (Atari, MSX, WonderSwan, Lynx, PC Engine,
etc.) repurpose SNES contacts for other cartridge buses; assuming
that the ordinary SNES ROM interface is the maximum used pin set is
unsafe. Published examples span different hardware generations and
might require additional bus lines that the Retrode3 currently leaves
unconnected. All compatibility claims need the **exact adapter
revision/schematic**, not name-only similarities.

The X1600E has ADC-capable input pins (VIN0-3), but the supplied 3.0
schematic shows them as unconnected. Digital pull-ups do not make a
GPIO an accurate resistor meter; a resistor-code design would require
an actual measurement arrangement (ADC + divider/protection and an
appropriate dedicated signal pin, or a defined multi-bit digital ID).
Do not mix this with console bus or detect pins.

## Decision and outstanding hardware homework

1. Inventory and pin-map **all** target Sanni SNES-plug adapters (incl.
   community adapters, not merely the HW5 internal backplane):
   list each SNES contact's connectivity, whether it is driven, whether
   it sees 5V / 3.3V, and whether it connects to GND or special logic.
2. Distinguish standard SNES ROM signals, SNES expansion contacts,
   Sanni adapter-specific usage, CIC, clock, audio and supplies.
3. Look for an existing universally safe, backwards-compatible ID
   contact. **None has been proven yet.**
4. If there is no such contact, do not sacrifice compatibility:
   keep manual/legacy software identification. For future
   hardware, discuss a new dedicated ID contact or compatible optional
   adapter-ID scheme with Sanni/Nikolaus.
5. Passive identification resistance is an economical proposal for
   mass-produced adapters, not yet a selected value, pin or electrical
   scheme. Check board input protection, signal levels and plugged-in
   power-down behavior before choosing resistor values.
6. Never infer permission to switch cartridge VCC to 5V solely from an
   uncertain software/analog signature. Keep voltage control separate.

Current status: **investigation**, no approved pinout, PCB change,
voltage policy implementation or driver patch.
