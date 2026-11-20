# Sega Master System SMS-0007: Retrode3 v3 experimental read-only core

Status: **mock tested, physical cartridge not tested**. Intended for the
Retrode2 SMS-0007 passive MegaDrive-to-SMS adapter and Retrode3 2.9.5.
Requires the separately installed Linux 7.3 `retrode3.ko` driver with fixed
D0..D7 adapter mode 20 (`retrode3-kernel/develop` changes). The user has
already tested MegaDrive with this updated driver. No kernel or DTB update
is included here.

## Physical routing (authoritative: user's SMS-0007.sch and SMS-0007.brd)

| MegaDrive edge | SMS connector | Meaning in this core |
|---|---|---|
| D0-D7 | D0-D7 | 8-bit read path, kernel mode 20 |
| A0-A14 | A0-A14 | Direct ROM offsets |
| A15 | A15 and /M0-7 | Low addresses enable first 32 KiB |
| A16 | /MC-F | Set high, prevent RAM selection |
| A17 | /M8-B | High below $8000, low for $8000-$BFFF |
| A21 | /WR | Set high (inactive) for all reads |
| A22 | /RD | Set low (active) for all reads |
| OE and CE | /CE (same net) | Kernel selects slot and pulses OE |
| VCC | VCC plus SMS /RESET | **5 V required**, no level conversion on the adapter |

SMS-0007 schematic also connects the unpopulated Game Gear footprint to
D8-D15, not to the SMS data pins. GG is a separate future core. The adapter
itself contains no active bus transceivers, voltage regulator, or mapper.

The complete MD slot address used by the 7.3 fixed-lane reader is:

    physical_address = A21(1) | A22(0) | A16(1) |
        (cpu_address < $8000 ? A17(1) : A17(0)) | cpu_address
    /dev/slot-md offset = (20 << 24) | physical_address

The complete physical address must stay below the MD slot's 23-bit limit.
Userspace uses `open(O_RDONLY)`, `lseek` and `read`; there is **no write(2)**
call to `/dev/slot-md`, no `/dev/mem`, no GPIO poking, and no voltage change.
Read windows never cross $8000 or $C000 boundaries.

## Electrical limitation to resolve before routine use

The current kernel's generic mode 20 calls `select_slot()` **before** setting
its new GPIO address. If the bus held an older A21=0, selecting SMS /CE before
A21 becomes high can transiently assert SMS /WR. This does not mean the
userspace module intentionally writes, but does mean that the current Linux
module alone does **not** guarantee a glitch-free /WR signal on first select.
For a proper hardware-safe release, use a kernel adapter read cycle which
prepositions /WR high and other address/control signals while /CE is inactive,
then selects /CE and reads. Verify also timing of /RD and /CE on an actual
adapter. Do not assume a good mock dump makes this electrically safe.

Power: SMS plug-in is a 5 V device. On the device, the driver exposes
`/sys/class/retrode3/slot-md/vcc` with `5000mV` or `3300mV`. The core checks
for the exact `5000mV` value before both probing and every 512-byte chunk.
It never writes to vcc itself. Insert/remove hardware only when powered off.
The kernel may reset MD VCC to 3.3 V on cartridge removal.

## Capability and opt-in boundary

- Core ID `sms-md`, physical slot `md`, generic NDJSON-v1 / ABI-v3.
- Linear ROMs **8, 16, 32 KiB** may be dumped experimentally.
- Sega-16-KiB mapper/banked ROMs 48 KiB..1 MiB have pure/mock-tested
  algorithms but live register writes are **rejected**.
- No SRAM/EEPROM/flash access, no cartridge register writes or auto-enabling.
- 3 bounded 16-byte reads at $1FF0, $3FF0, $7FF0 recognize `TMR SEGA` for a
  ROM-size *hint*. There is no title database, and final ROM is reported as
  `verification.status=not_checked`, `verified=false` until external CRC check.
- If `RETRODE3_SMS_MD_EXPERIMENT` is absent, `fast_probe` does no device I/O,
  even if a plugin-enabled browser automatically calls `probe`.
- The exact experimental opt-in is:

    RETRODE3_SMS_MD_EXPERIMENT=enable-readonly-v1

The environment variable unlocks *only* the read-only linear path. It cannot
unlock bank writes or skip the VCC verification. It has no effect on other
cores. This is a deliberate first-lab feature, not general release support.

## Local source installation and tests on retrode3-oscr/develop

Unpack this archive beside the checked-out `retrode3-oscr` repository.
Run `python3 retrode3-sms-md-development/apply_to_checkout.py /path/to/retrode3-oscr`
to copy sources, update `Makefile.migration`, and retain existing files.
The script refuses to overwrite conflicting files. Then in the checkout:

    make -f Makefile.migration test-sms-md-module
    ./tools/preflight-ci.sh

Only after that full test passes should changes be committed to `develop`.
Do not put lab SMS into `stage-all-shared-cores` or master until real hardware
and MD/SNES/NES regressions have passed. No shared-CLI rebuild is required
by the `native-sms-md-module` target.

Test/deploy the locally compiled `build/migration/sms-md.so` together with
`packaging/cores/sms-md.core` under the usual
`/usr/lib/retrode3/cores-available/`, then enable **manually** using
`retrode3-corectl enable sms-md`. Do not auto-install in Debian packages yet.

## Explicit experimental CLI workflow

First inspect actual `vcc` path/value on the Retrode3 and set the appropriate
5 V using the existing driver's sysfs interface, with the *hardware powered
off during insertion* and before any SMS read. Since this is unverified
hardware, check voltage and /WR timing externally before plugging in any
valuable SMS cart. Then run the common CLI manually (not the Web autoprober):

    RETRODE3_SMS_MD_EXPERIMENT=enable-readonly-v1 \
      /usr/bin/retrode3-cli --protocol ndjson-v1 --interactive

Input lines (one JSON object per line):

    {"command":"get_capabilities","core":"sms-md","request_id":"cap"}
    {"command":"probe","core":"sms-md","request_id":"probe"}
    {"command":"read_rom","core":"sms-md","request_id":"rom32","output":"/tmp/sms-test.sms","selection":{"version":1,"mode":"override","schema_id":"sms-md-layout-v1","parameters":{"mapping":"linear","rom_kib":32}}}

Probe may yield `read_layouts:[{"id":"linear-32",...}]` where a valid
SMS header declares 32 KiB. A smaller header may represent a larger ROM:
verify the PCB and use an explicitly chosen safe ROM size. CRC matching
against a separate known-good image is needed before calling a dump correct.
If no 5 V is present or opt-in is missing, read is denied. Failed reads must
never publish partial output via the common CLI.

## Validation performed while creating this archive

`make -f Makefile.sms-md test` passed (C++11 -Wall -Wextra -Werror -pedantic),
including header parser, pure banked-reader mocks, real v3 callbacks, sparse
POSIX slot-offset mock, VCC/opt-in rejection, and no-write path. The compiled
`build/sms-md.so` in this archive, if included, is a **host x86_64 test
library, NOT installable on MIPS**. Rebuild using the target toolchain or on
the actual Retrode3. The repo-wide `./tools/preflight-ci.sh` and physical
cartridge tests have **not** been run in this isolated source package.
