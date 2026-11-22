# Retrode2 GBx-0011 adapter on the MegaDrive slot (GB/GBC/GBA)

Status: **portable ROM-only reader mock-tested**; **no physical GBx bus
transport, no OSCR firmware integration and no Retrode3 `.so` yet**.
This is a feature-branch experiment, not a released or voltage-safe core.

## Source of truth and scope

The adapter netlist was inspected directly from user-supplied Eagle
`GBx-0011.sch` and `GBx-0011.brd` (Eagle 6.4).  Do not infer a generic
GBx pinout from its name: this board routes a 32-pin GB/GBA connector
through a **MegaDrive** edge and also has an unpopulated N64 connector.
There is no voltage regulator or level shifter on the GBx-0011 PCB.

The reference cartridge algorithms are Sanni's pinned OSCR firmware
`src/cores/GameBoy.cpp` and `src/cores/GameBoyAdvance.cpp` (submodule
`third_party/oscr-firmware`). Their original files remain untouched.
In particular, the low-level GB and GBA bus cycles in that firmware
assume the *native* GBx connector, **not** the GBx-0011 MD edge.

| MD edge logical signal | GBx connector | GB/GBC electrical meaning | GBA electrical meaning |
|---|---|---|---|
| D0..D15 | pins 6..21, AD0..AD15 | A0..A15 (address outputs) | multiplexed AD0..AD15 (bidirectional) |
| A8..A15 | pins 22..29, A16..A23 | D0..D7 (data inputs) | A16..A23 (upper address outputs) |
| A2 | pin 4, /RD | read strobe | read strobe |
| A3 | pin 5, /CS_ROM | chip select | ROM chip select / address latch |
| A5 | pin 30, /CS_RAM | GB reset pin | RAM chip select |
| A6 | pin 3, /WR | write strobe | write strobe |
| A20 | pin 2, CLK | clock | clock |
| A19 | pin 31, IRQ | audio pin | interrupt |
| VCC/GND | pins 1/32 | pass-through | pass-through |

Important: GB/GBC and GBA interpret the same 32 connector contacts
**differently**. MD `OE` and `CE` are wired to the *unpopulated* N64
connector and not to the GBx `/RD` and `/CS_ROM` lines. A normal MD
ROM read therefore cannot be used as a GBx transaction. The separate
MD A0/A1/A4/A7 lines are wired to N64-only signals.

## Safety conditions before any live transaction

* GB/GBC expects a different supply profile than GBA. Original Sanni
  `GameBoy::cartOn()` explicitly requests **5 V**, while
  `GameBoyAdvance::cartOn()` requests **3.3 V**. The Eagle net's
  `+3V3` label does **not** insert a regulator. Never automatically
  try a 5-V GB probe against a potentially inserted GBA game.
* Until a trusted cartridge-family identification method exists,
  require an explicit GB/GBC vs. GBA selection before applying the
  corresponding voltage. `slot-md/sense` proves only MD-slot presence.
* Verify the board and host electrical path for MD data-bus output and
  MD address-bus input. A GPIO/software DDR setting alone does not prove
  an intervening hardware buffer is bidirectional. In particular, the
  proposed original OSCR firmware route must be checked against the OSCR
  board's address-bus drive hardware, not just AVR register names.
* Hold `/WR` inactive throughout all ROM reads. For GB/GBC keep the
  reset/CS-pin semantics appropriate to the inserted cartridge; for GBA
  hold `/CS_RAM` inactive. Never emit RAM/save/flash writes.
* The GB pin 31 **audio output** / GBA pin 31 **IRQ** is connected to
  MD physical `A19`, normally a host-driven address output. Explicitly
  tri-state and verify that line before powering a cartridge; otherwise
  the host and cartridge could drive against each other. `A20` is the
  cartridge clock and must likewise have a defined, safe idle level.
* Set data-direction and address lines **with cartridge selects inactive**;
  after settling, issue the `/CS_ROM` and `/RD` cycle and return to a
  safe inactive state. Validate with a logic analyzer or scope before
  connecting a valuable cartridge.
* Hot-swap and cancellation must restore inactive controls, released
  data lines and the earlier supply, including error paths.

## Two distinct transport operations

1. `gb_read8(cpu_addr)` places 16-bit GB address `A0..A15` on the MD
   **D0..D15 outputs**, makes MD `A8..A15` an **8-bit data input**, then
   drives GB `/CS` and `/RD` at MD `A3` and `A2`. It samples MD
   `A8..A15`, not MD `D0..D7`. This is the opposite of normal MD reads.
   MD `A19` remains in high impedance (cartridge output/audio pin).
2. `gba_read16(byte_addr)` divides the byte address by two (OSCR's
   GBA word addressing); drives the 16 low bits on MD `D0..D15` and
   the upper eight on MD `A8..A15`, pulses GBA `/CS_ROM` to latch the
   multiplexed low address, **releases MD D0..D15 to input**, asserts
   `/RD`, and reads the returned 16-bit word from MD `D0..D15`.
   MD `A19` remains in high impedance (cartridge IRQ pin).
   Implement both operations as **atomic transactions in the host HAL**,
   not separate userspace toggles of individual pins.

The Letux 7.3 MD `MD_ROM` reader and the existing adapter modes 20/21
only sample MD data pins with the MD address bus driven. They cannot
provide either GB8 or GBA16 cycle. Kernel support therefore requires
new, explicit, narrowly scoped MD-slot modes, with safe direction
handoffs, removal checks and a strictly separate authorization for any
future MBC-register writes. Reusing SMS mapper-write mode 22 would be
incorrect.

## Portable OSCR-compatible ROM reader: first incremental source

`include/gbx/retrode2_readonly.h` plus `src/gbx/retrode2_readonly.cpp`
use C++11, no Linux or AVR headers, and a three-callback transport
(`gb_read8`, `gba_read16`, `cancelled`). They can be compiled into an OSCR
backend **or** a Retrode3 v3 core adapter once either platform supplies
validated transactions. Their small metadata/ROM loops follow the
relevant Sanni GB/GBA header checksum and read conventions, but this
first scaffold is not claimed to be the complete generated Sanni core.

* **GB/GBC:** read-only 0x100..0x14F header probe, checksum algorithm
  matching Sanni `GameBoy::checkCart()`, and exactly 32 KiB ROM read for
  no-MBC cartridge codes `0x00/0x08/0x09` with ROM size code `0`.
  MBC1/2/3/5 and special cartridges are rejected, not half-dumped.
* **GBA:** read-only first 192 bytes, Sanni logo-byte-sum and GBA
  complement checksum, title and product-code extraction; ROM streaming
  in little-endian word order using an explicit 1/2/4/8/16/32 MiB size.
  GBA has no reliable physical ROM size in its cartridge header.
  The special 32-MiB + EEPROM padding correction from Sanni's reader
  has not yet been ported; this stage outputs raw bus bytes only.
* The reader re-probes before a ROM transfer and compares basic header
  metadata to a previous probe when provided. It cannot guarantee that
  a removed-and-reinserted cartridge with an identical header is the
  same physical object; the host must enforce slot sense, locking and
  cancellation. The host owns final CRC32, database lookup, atomic
  publication and `verified` status. No match means **not verified**.

Local mock validation, without accessing `/dev/slot-md`:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude \
  src/gbx/retrode2_readonly.cpp tests/retrode2_gbx_readonly_test.cpp \
  -o /tmp/retrode2_gbx_readonly_test
/tmp/retrode2_gbx_readonly_test
```

Mocks check GB header and exact 32-KiB output, MBC rejection,
header-change rejection, GBA header checks, exact 1-MiB word-order
output, size rejection and cancellation. **No cartridge was connected.**

## Next implementation gates

1. Verify the Retrode3 hardware MD bus direction, OE/CE isolation,
   physical controller pin capabilities and GB/GBA voltages. Then add
   a separately guarded kernel branch, building **only**
   `drivers/misc/retrode3/retrode3.ko` using the prepared MIPS tree.
2. Implement a matching **original OSCR AVR MD-slot backend** and a
   **Retrode3 typed HAL backend** for the same `gb_read8/gba_read16`
   contract. Keep original Sanni sources pinned and unmodified; upstream
   the portable adapter and AVR backend only after review.
3. Add `gb-md` and `gba-md` **separate** v3 `.so` adapters (physical slot
   `md`), with explicit hardware profiles, safe probe order, generic
   selection/override schema and no per-system Web code. MBC bank writes
   require further explicit kernel, timing and SRAM-safety acceptance.
4. Run the repository's complete `./tools/preflight-ci.sh`, MIPS build,
   physical ROM CRC comparison and regression tests for MD/SMS/SNES/NES.
   Only then consider squashing from the feature branch to `develop`,
   and subsequently reviewed release to `master`.
