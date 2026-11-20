# NES v3 regional recognition: real-device investigation, 2026-11-20

**develop only. Not master. PAL Mario & Yoshi ROM data physically verified; automated recognition with the newly added fingerprints still awaiting on-device confirmation.**

## Verified PAL Mario & Yoshi cartridge and new fingerprint (2026-11-20)

Five consecutive normal **read-only v3 probes** without moving the
Mario & Yoshi cartridge reported the exact same CRC32 pair:

- `cpu_8000_crc32 = 29D1D575`
- `cpu_e000_crc32 = 9A78A49D`

The same cartridge had previously reported `NES slot empty/unreadable`
twice, possibly from transient contacts. The five stable probes and
subsequent dump establish that the ROM chips are not defective.

Using the ordinary `read_rom` v3 **override** for
MMC1/submapper 0, PRG 128 KiB, CHR 32 KiB, no battery,
a complete real-device iNES ROM of **163856 bytes** was obtained.
Every checksum matched the original European cartridge reference:

| Section | Real hardware CRC32 | Reference CRC32 | Result |
| --- | --- | --- | --- |
| PRG-ROM, 128 KiB | `1406783D` | `1406783D` | MATCH |
| CHR-ROM, 32 KiB | `FC0BBA23` | `FC0BBA23` | MATCH |
| PRG+CHR, 160 KiB | `836685C4` | `836685C4` | MATCH |

This physically validates the NES-SFROM MMC1 PRG/CHR-ROM reader.
The only confirmed recognition defect was the absent PAL probe
fingerprints in Sanni's catalogue.

The supplemental `Mario & Yoshi (Europe)` record now uses
**both physically measured signatures**. A normal v3
probe should offer `m1-128-32` and
`selection_required:false`; an unmodified v3 auto read should
verify the full CRC against `836685C4`.
Mock tests use purely synthetic bytes adjusted to these CRCs;
there are no copyrighted game ROM bytes in the repository.

**Remaining:** Reinstall just the updated NES core and verify that
automatic detection works on the actual cartridge.

## Symptoms on the user's Retrode3

- PAL Rad Racer incorrectly offered as *Highway Star (Japan)
  (Famicom 3D System)*, followed by a bad/incorrectly labeled read
- PAL Mario & Yoshi: no fast-probe recognition
- PAL Dr. Mario: fast-probe recognizes the title correctly (regression)
- PAL Faxanadu: no fast-probe recognition

## Why the wrong title appeared

The Sanni OSCR NES DB (pinned oscartreader/oscr-databases,
commit `87a48948c6a04260205f148d313614cc44e05cd5`) contains
*Highway Star (Japan)*, but omitted both regional Rad Racer titles
and PAL Mario & Yoshi. A fast-probe is NOT a unique ROM identity:
it reads 512 bytes of the CPU PRG view at $8000 plus 512 at $E000,
calculates two independent CRC32s and compares either to the
Sanni `id32a`/`id32b` identifiers. MMC1 power-on bank selection
and common game code can make these fingerprints inconclusive.

Sanni's mapper-1 reference:
- Highway Star (Japan): 128 KiB PRG / 0 KiB CHR, PRG CRC32
  `02589598`, id32a `402629F3`, id32b `F94CC110`.
- Faxanadu (Europe): 256 KiB PRG / 0 KiB CHR, PRG CRC32
  `76C161E3`, id32a `6EB0C4EF`, id32b `435FB786`.
- Dr. Mario (Europe): 32 KiB PRG / 32 KiB CHR, MMC1
  submapper 5, full PRG+CHR CRC32 `9735D267`.

Independent original cartridge references:
- PAL Rad Racer (NES-SGROM, Mapper 1): 128 KiB PRG,
  no CHR-ROM (8 KiB CHR-RAM), **PRG CRC32 `654F4E90`**
  (nesCartDB profiles 2124, 2698; physical Retrode3 SGROM test
  from 2026-11-20). USA PRG CRC32 `8B9D3E9C`.
- PAL Mario & Yoshi / Yoshi (NES-SFROM, MMC1): 128 KiB PRG,
  32 KiB CHR-ROM, **combined payload CRC32 `836685C4`**
  (nesCartDB profile 1997). PRG chip `1406783D`, CHR chip
  `FC0BBA23` separately. No battery.
- PAL Faxanadu (NES-SGROM, MMC1): 256 KiB PRG,
  no CHR-ROM, **PRG CRC32 `76C161E3`**
  (nesCartDB profile 2368). No battery.

## Implemented changes

- Three tiny supplementary regional entries are appended in
  `oscr_nes_adapter.cpp` without altering the pinned generated
  Sanni catalogue or importing ROM bytes:
  `Rad Racer (Europe)`, `Rad Racer (USA)`, and
  `Mario & Yoshi (Europe)`.
- The **PAL Rad Racer** supplemental record uses the proven
  Highway Star fingerprints as *potential* common-family identifiers
  because the PAL module was misidentified as that entry on real
  hardware. Mario & Yoshi PAL now has two **real,
  independently observed** PRG-bus probe fingerprints recorded
  after complete verified hardware dumping. The USA Rad Racer entry
  deliberately retains no invented fingerprint.
- Probe with the Rad Racer prefix now offers multiple possible titles
  but **one shared supported mapper-1 128/0 layout**:
  `ambiguity:"titles_only"`, `selection_required:false`.
  The on-insert probe never identifies a title as ROM-CRC-verified.
- The complete CRC selects the correct regional reference and its
  ROM header. Selecting the wrong Japanese candidate produces
  `verification.status:"mismatch"`, never `verified:true`.
- Generic v3 `probe_result.probe_fingerprints` now reports
  `cpu_8000_crc32` and `cpu_e000_crc32`. These are
  checksums already calculated during the same **1024-byte
  write-free fast-probe**, not additional bus accesses.
  Legacy NES v2 ABI is unchanged.
- Tests use only **synthetic** 128/0, 128/32, and 256/0 ROM
  patterns patched to the documented full CRCs; no copyrighted
  original game bytes are stored. NES and shared registry CI
  passed at commit `4ec3cd327e02d28cb2b854f8b0f52b6aecf34524`.

## Follow-up testing on the actual Retrode3

After updating **only the normal NES plugin** to this `develop` build,
run this command with each cartridge separately:

```sh
printf '%s\n' '{"command":"probe","core":"nes","request_id":"nes-check"}' |
  /usr/bin/retrode3-cli --protocol ndjson-v1 --interactive
```

Send the full `probe_result` for Rad Racer PAL, Mario & Yoshi PAL,
Dr. Mario PAL, and Faxanadu PAL. Particularly important:
`probe_fingerprints`, `identification_status`,
`ambiguity`, `selection_required`, `candidates`,
`read_layouts`.

This uses the shared CLI's *interactive* v3 path; the legacy one-shot
probe may choose a v2-compatible output without the new diagnostics.

If automatic detection is still unknown, **do not guess a ROM size
from the first 512 bytes**. For known original PAL PCBs choose
these reviewed manual overrides through v3 Web/CLI:
- Mario & Yoshi: mapper 1, submapper 0, PRG 128, CHR 32, battery false
- Faxanadu: mapper 1, submapper 0, PRG 256, CHR 0, battery false
- Rad Racer: mapper 1, submapper 0, PRG 128, CHR 0, battery false

Only after a complete dump can the full CRC confirm the title.
If the reported CRC does **not** match, retain the output as
`verified:false` and collect per-bank checksums before changing
electrical mapper routines. No writes at all during insertion/probe,
and no battery-backed MMC1 cartridge tests until safe kernel timing
and tested SRAM backup/restore exist.

For Faxanadu, the missing fast match is **not yet explained**.
The new live `cpu_8000_crc32`/`cpu_e000_crc32` values will show
whether Sanni's two expected signatures are absent and permit a
hardware-level diagnosis without guessing. A silent auto-dump using
a guessed layout would hide the problem instead of fixing it.

## Sources

- Sanni pinned NES JSON blob `b07cfd2943b986f8ffc85e61722dbb2e440cec37`
- nesCartDB profiles: 2124, 2698, 1997, 2368
- <https://www.romdetectives.com/Wiki/index.php?title=Rad_Racer_%28NES%29>
- `docs/nes-mmc1-general-lab.md` for earlier physical SGROM acceptance
