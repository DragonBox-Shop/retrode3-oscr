# NES mapper bank switching: develop-only prototype (2026-11-20)

The single `retrode3-cli` and `nes.so` now contain the first
Sanni-derived bank-switch sequencer. The original firmware submodule is
**unchanged**, pinned at `e5a08326dfc8fff67193f7fc5a7bc36b5282b28c`.
This is **not** a claim of electrical acceptance or production support.

## Mapper support levels

| Mapper | PRG/CHR strategy ported | Mock | Default production | Why |
| --- | --- | --- | --- | --- |
| 0 NROM | Linear 16/32K PRG, 0/8K CHR | Yes | Read-only enabled | Previous physical CRC; new v2 needs regression |
| 1 MMC1 | 32-512 KiB PRG + 0-128 KiB CHR, 695 Sanni DB records and opt-in lab reader | NES CI mock PRG+CHR and DB tests pass | Rad Racer PAL 128/0 verified; 32/32 and 128/32 hardware pending; production blocked | Generic PRG writes not safe for battery-backed PRG-WRAM; kernel NES_REG timing unverified |
| 2 UxROM | Switch 16K PRG at $8000, last 16K fixed | Yes | Blocked | Bus-conflict checks and CPU register-write timing await physical review |
| 3 CNROM | Fixed PRG, switch 8K CHR | Yes | Blocked | Bus-conflict checks and CPU register-write timing await physical review |
| 4 MMC3 | Switch 8K PRG using R6; 1K CHR using R2 | Yes | Blocked | Bank-write timing and MMC3 variants await hardware verification |
| 7 AxROM | Switch full 32K PRG at $8000 | Yes | Blocked | Register-write timing and mirroring variants await hardware verification |

## MMC1: pinned database and PRG+CHR hardware lab (2026-11-20)

See [general MMC1 hardware tests](nes-mmc1-general-lab.md) for current
commands, operator gates, and safety constraints. Old
[Rad Racer SGROM reference](nes-mmc1-sgrom-lab.md) remains a valid
single-board physical acceptance record.

Sanni original `NES.cpp` bank sequences are represented in
`nes_mmc1_plan.cpp`: 32KiB PRG linear or repeated 16KiB PRG
register banks; 8KiB CHR banks. We additionally force MMC1
control 0x0C before each CHR bank, so the read does not depend
on stale 4KiB CHR mode. The general conservative lab geometries:
PRG 32/64/128/256/512 KiB; CHR 0/8/16/32/64/128 KiB;
submapper 0 and submapper 5 with 32KiB PRG. CHR=0 means CHR-RAM,
never dumped as ROM.

The **pinned Sanni ROM metadata** is compiled into
`src/retrode3/oscr_nes_mmc1_catalog.cpp`: 695 mapper1 records
from `oscartreader/oscr-databases` commit
`87a48948c6a04260205f148d313614cc44e05cd5`, original
source blob `b07cfd2943b986f8ffc85e61722dbb2e440cec37`.
`iprg` is in 16 KiB, `ichr` is in **4 KiB** units. Generator:
`tools/gen-nes-mmc1-catalog.py`. All ROM bytes are absent,
only metadata/title/fingerprints/header/CRC. `fast_probe` still
reads two 512-byte PRG blocks and **never** switches banks; candidate
IDs and layout IDs are returned without writes. Unique or ambiguous
candidates may require layout selection; unknown fast-probe entries can
be identified by exact complete payload CRC after a manual dump.
NROM behavior is kept.

The real MMC1 executor is **only** in the separately built
`RETRODE3_NES_MMC1_LAB` core, requiring the exact runtime opt-in
`RETRODE3_MMC1_TEST_ACK=NO_BATTERY_TEST_ONLY` and a
`battery=0` layout. The legacy Rad Racer-specific opt-in still
works **only** for mapper1 128 KiB PRG / no CHR. Standard
`native-nes-module`, `stage-retrode3-runtime` and
`get_capabilities` remain MMC1 write-disabled; no Web/auto-dump
production enablement. The lab swaps `.so` only for one run and
restores the previous version via `tools/nes-mmc1-lab.sh`.

**Risk boundary:** Sanni's original `write_reg_byte` protects
PRG-WRAM from accidental $6000-$7FFF writes while switching MMC1
at $E000/$F000. Current kernel `NES_REG` does not guarantee this
M2/ROMSEL pulse; lab uses ordinary NES_MODE_PRG writes. Therefore
only personally verified **no-battery** test cartridges with
disposable RAM data may be used; absolutely no customer saves or
battery-backed Final Fantasy cartridges. Backup and restore are
not yet implemented. A successful SGROM PRG CRC does not validate
save-safe timing.

**Physically verified:** Rad Racer PAL SGROM on the real Retrode3,
131088 bytes, payload CRC32 `654F4E90` exactly matching reference,
file CRC32 `B78FDA39`, core restored. Other layouts **not**
hardware-proven. **GitHub Actions CI** passed the mapper tests,
C++11 compilation and lab build on `develop` at `31d9ab02`;
new documentation/tool commits follow afterward.

**Never** enable MMC1 through general
`RETRODE3_NES_MAPPER_WRITE_EXPERIMENTAL`; use the separate
narrowly gated lab only.

Bus-conflict handling for UxROM and CNROM scans the fixed PRG region for
bytes equal to the target bank values **before** the first write.
Unlike Sanni's generic fallback, a missing conflict-free address stops
with an error and no writes. This means the first banked read may involve
an additional fixed-bank scan, but never an extra whole-ROM identification
dump. During a scan cancellation is polled once per 512-byte block.

The MMC3 source also contains `write_prg_byte(0xA001, 0x80)`, which
enables writable PRG RAM. We deliberately omit it for ROM dumping;
no save/RAM/flash writes are required. Other MMC3 derivative mappers
are not enabled.

## API and supported manual geometries (experimental only)

Interactive CLI `read_rom` accepts all fields shown here for a manual
layout, with strict integer bounds and no mixing with candidate IDs:

```json
{"command":"read_rom","core":"nes","request_id":"test-m2","output":"/tmp/m2.nes","mapper":2,"submapper":0,"prg_kib":64,"chr_kib":0,"mirroring":"horizontal","battery":0}
```

- Mapper 2: PRG 64/128/256 KiB, CHR 0
- Mapper 3: PRG 16/32 KiB, CHR 8/16/32 KiB
- Mapper 4: PRG 32/64/128/256/512 KiB, CHR 0 or 8/16/32/64/128/256 KiB
- Mapper 7: PRG 64/128/256 KiB, CHR 0

Submapper must be 0. These are conservative **software** geometries,
not guarantees that every PCB carrying that mapper matches Sanni's
bank switching electrically. The current automatically matched catalogue
still contains **Mapper 0 only**; unknown banked cartridges require
manual layout, and their ROMs always report `verified:false`.
The iNES file header is derived from the explicit user geometry.

## Developer tests

```sh
git submodule update --init third_party/oscr-firmware
make -f Makefile.migration test-native-nes-module
make -f Makefile.migration test-all-shared-cores
```

`tests/mock_nes_kernel.cpp` emulates separate PRG/CHR bank spaces and
checks bus-conflicting register writes. Tests compare byte-for-byte
complete ROMs and CRCs for 2/3/4/7, probe-without-writes, missing safe
addresses, MMC1 refusal, invalid geometries and write failure cleanup.
`tools/verify-nes-mapper-sequences.py` checks the pinned Sanni register
order against known upstream method fragments.

## Physical acceptance before enabling

1. Verify running kernel branch/commit and actual board revision, slot
   voltage, CPU_M2/ROMSEL timing, and safe driver register writes.
2. Specifically fix/review the `NES_MODE_REG` timing problem before
   attempting MMC1; keep saves backed up and cartridges protected.
3. Build a separate experimental image and test a known, nonvaluable
   mapper-2/3/4/7 cart using exact manual geometry. Check physical read
   lengths, expected full CRC32, stability, removal/cancellation, and
   that save RAM remains intact. Never auto-write on insertion.
4. Only promote each mapper capability after documented physical
   reference dumps. Review the release gate and leave `master`
   untouched until the kernel + userspace combination is safe.

The generic web UI currently has no mapper selection for these new
experimental cores; its separate integration belongs to the OS repo.
