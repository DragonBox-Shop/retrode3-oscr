# Retrode2 GBx-0011 adapter on the MegaDrive slot (GB/GBC/GBA)

Status: **portable ROM-only reader mock-tested** and **real Retrode3 manual
GB/GBA header probes passed at 3.3 V** on separate feature branches.
One **complete 16 MiB GBA cartridge dump** has now passed the known
reference CRC32 and SHA256 checks. MBC support, GB/GBC full ROM dumps,
GBA >16 MiB window testing, electrical validation, OSCR firmware
integration and Retrode3 dynamic `.so` plugins are **not yet accepted**.
This remains a feature-branch experiment, not a released production core.

## Physical validation so far (2026-11-23)

The experimental Letux 7.3 kernel `feature/gbx-md-read-7.3` and this
branch's `tools/gbx-md-hwtest.py` successfully probed a real Game Boy
**PINBALL** cartridge twice with byte-identical valid headers
(`type=01`, `ROM=01`, `RAM=00`), and a real GBA **SHININGFORCE**
cartridge (`game_code=AF5P`) with a valid GBA Nintendo-logo sum and
header checksum, all at **3.3 V**. GBA originally returned `0xFF` or
wrong sequential ROM words. A consistent **+1 offset** in the adapter
MD A-pin to Retrode3 `addr-gpios[]` mapping affected both the GB data
window and GBA control lines. Correcting the entire adapter-specific
GPIO mapping yielded a valid GBA header. The later 1 MiB burst comparison
and complete 16 MiB GBA reference-matched dump below extend this evidence
beyond headers; GB/GBC banking, >16 MiB GBA window access, and
GBC-specific tests remain outstanding. **On 2026-11-23,
regression after the complete control-line remap passed:** two repeated
GBA `--raw` probes of `SHININGFORCE` / `AF5P` returned byte-identical
valid 192-byte headers, followed by two repeated GB `--raw` probes of
`PINBALL` / `type=01 ROM=01 RAM=00`, also byte-identical and
checksum-valid. All at 3.3 V on the same corrected test module.
The standard MegaDrive driver and web interface were not changed for
this experiment.

## Verified 1 MiB GBA burst test (2026-11-23)

Shining Force GBA `AF5P` was read as two 1 MiB files with the baseline
random-access transport (mode 24) and once with the opt-in sequential
burst transport (mode 28). The baseline copies were byte-identical.
The experimental burst copy was also **byte-identical** to baseline
according to `cmp`, CRC32 `309284CE` and SHA256
`b26b394da0f5e3dd91442cea2dbe530ed49f534ca491c7facb672092659357ef`.
Both readers passed the GBA header check at 3.3 V.

Measured `time` on the target Retrode3:

| Mode | 1 MiB real | user | sys | Throughput |
|---|---:|---:|---:|---:|
| Random access (24) | 59.368 s | 0.919 s | 55.311 s | 17.3 KiB/s |
| Sequential burst (28) | 8.943 s | 0.848 s | 7.157 s | 114.5 KiB/s |

Speedup approximately **6.64x** for this 1 MiB test. This is
**hardware-validated for one 1 MiB ROM prefix**, not proof of
full-cartridge correctness, behavior at 16 MiB window boundaries,
or correctness with other cartridges. Keep the baseline modes 24/25
intact and the burst mode opt-in until further acceptance tests.

## Complete 16 MiB GBA reference-matched ROM dump (2026-11-23)

Real Shining Force GBA cartridge (`title='SHININGFORCE'`, game code
`AF5P`) was dumped from the Retrode3 at 3.3 V using the opt-in
sequential burst reader (mode 28):

```sh
time python3 tools/gbx-md-hwtest.py gba --fast --mib 16 --dump /tmp/shiningforce-full.gba
wc -c /tmp/shiningforce-full.gba
sha256sum /tmp/shiningforce-full.gba
```

Hardware results:

- Length: **16,777,216 bytes (16 MiB)**
- CRC32: **`07B06994`** (matches previously identified ROM reference)
- SHA256: **`8a8485e100d9747e44e3a4255058af7cc2a425709a7172fcddd05ad87cc2c1e1`**
  (matches the previously identified ROM reference)
- Wall clock **2m04.915s**, user **4.019s**, system **1m54.402s**
- Average throughput about **131 KiB/s**
- A prior 1 MiB cross-check also matched the slow reference reader
  byte-for-byte (`cmp`, CRC32, SHA256)

This is the **first full-ROM GBA hardware acceptance result** for
this experimental transport. It confirms the complete first 16 MiB
address window on this cartridge, including block-boundary handling.
It does **not** yet validate the 16-32 MiB second window, other GBA
cartridges, save types, GB/GBC MBC switching, voltage transitions, or
normal-plugin operation. Keep all changes in feature branches until
those remaining integration tests are performed. The kernel's
`develop` branch was already advanced to the GBx bench commit for
other kernel work; do not alter it merely to record this benchmark.

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
* **All probes use 3.3 V, without exception**, for both GB/GBC and
  GBA. A probe must never raise the slot to 5 V, including on
  invalid, ambiguous or absent headers. A GB/GBC cartridge may not
  respond reliably at 3.3 V: classify that result as inconclusive,
  not as proof of GBA or an empty slot. Do not retry at 5 V in the
  automatic probe.
* The first physical test selects GB/GBC or GBA manually. Later, the
  generic host may probe both header formats at 3.3 V and select a
  family only when sufficiently identified. `slot-md/sense`
  proves only MD-slot presence.
* During **ROM reading only**, apply the selected family profile:
  GB/GBC **5 V**, GBA **3.3 V**, analogous to SMS-MD hardware
  profiles. Revalidate the selected family and cartridge presence
  before switching voltage; restore the previous hardware profile
  on completion, failure and cancellation.
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

For a manually selected physical hardware test, after building and
loading the experimental kernel module from branch
`feature/gbx-md-read-7.3`, run with **no GB/GBC/GBA cartridge inserted**:

```sh
python3 tools/gbx-md-hwtest.py arm
```

This sends an empty GBx bench read that puts MD A19 in high
impedance. Only then insert the cartridge and run the desired probe:

```sh
python3 tools/gbx-md-hwtest.py gb
python3 tools/gbx-md-hwtest.py gba
```

Only run the command for the actually inserted cartridge family. The probe
always applies 3.3 V. After a valid manual GB/GBC probe the optional
no-MBC 32-KiB dump can switch to 5 V and restore 3.3 V:

```sh
python3 tools/gbx-md-hwtest.py gb --dump /tmp/gbx-test.gb
python3 tools/gbx-md-hwtest.py gba --mib 1 --dump /tmp/gba-test.gba
```

The GBA ROM size must be supplied explicitly. The test is not yet a
dynamic CLI plugin; normal cartridge bus modes remain blocked while
the GBx bench is armed or has been used. Unplug the adapter, unload
and reload the module before returning to normal MD/SMS/SNES/NES work.

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
   `md`), each with a 3.3-V probe profile; GB/GBC reading uses 5 V,
   GBA reading 3.3 V. Provide safe probe order, generic
   selection/override schema and no per-system Web code. MBC bank writes
   require further explicit kernel, timing and SRAM-safety acceptance.
4. Run the repository's complete `./tools/preflight-ci.sh`, MIPS build,
   physical ROM CRC comparison and regression tests for MD/SMS/SNES/NES.
   Only then consider squashing from the feature branch to `develop`,
   and subsequently reviewed release to `master`.
