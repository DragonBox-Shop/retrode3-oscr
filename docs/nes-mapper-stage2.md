# NES mapper bank switching: develop-only prototype (2026-11-20)

The single `retrode3-cli` and `nes.so` now contain the first
Sanni-derived bank-switch sequencer. The original firmware submodule is
**unchanged**, pinned at `e5a08326dfc8fff67193f7fc5a7bc36b5282b28c`.
This is **not** a claim of electrical acceptance or production support.

## Mapper support levels

| Mapper | PRG/CHR strategy ported | Mock | Default production | Why |
| --- | --- | --- | --- | --- |
| 0 NROM | Linear 16/32K PRG, 0/8K CHR | Yes | Read-only enabled | Previous physical CRC; new v2 needs regression |
| 1 MMC1 | Isolated abstract bank plan + separate RAM-less SGROM lab reader (128K PRG/CHR-RAM only) | C++11 plan tested locally; full mocked suite pending | Blocked (lab build explicit opt-in only) | Generic PRG write unsafe for WRAM; the NES_REG fast edge is unverified |
| 2 UxROM | Switch 16K PRG at $8000, last 16K fixed | Yes | Blocked | Bus-conflict checks and CPU register-write timing await physical review |
| 3 CNROM | Fixed PRG, switch 8K CHR | Yes | Blocked | Bus-conflict checks and CPU register-write timing await physical review |
| 4 MMC3 | Switch 8K PRG using R6; 1K CHR using R2 | Yes | Blocked | Bank-write timing and MMC3 variants await hardware verification |
| 7 AxROM | Switch full 32K PRG at $8000 | Yes | Blocked | Register-write timing and mirroring variants await hardware verification |

## MMC1: hand-operated RAM-less SGROM lab (2026-11-20)

See [physical lab instructions](nes-mmc1-sgrom-lab.md). The pure
`nes_mmc1_plan.cpp` remains the source of abstract bank operations.
An additional executor in `oscr_nes_mapper.cpp` now translates its
reset/LSB-first 5-bit writes and bank reads into the existing typed
`nes_write(..., NES_MODE_PRG)` kernel interface, **only** when
compiled as `RETRODE3_NES_MMC1_LAB` (or in the mock fixture) and
when the exact per-process runtime acknowledgment is present.
The standard `native-nes-module` and staged `nes.so` continue to
reject all MMC1 register writes; the special lab `.so` is never
included in `stage-retrode3-runtime`.

The real experimental reading geometry is intentionally **only**
MMC1/submapper 0, PRG 128 KiB, CHR 0 KiB, battery 0, with a
**physically verified no-PRG-WRAM Rad Racer NES-SGROM**.
The general abstract plan supports more sizes for offline tests, but
these geometries are **not executable** on real hardware.
The lab run is manual, never auto-probed as supported, and
`get_capabilities` stays `read_supported:false` for Mapper 1.
The script `tools/nes-mmc1-sgrom-lab.sh` asks the operator to confirm
the PCB, swaps the lab `.so` only for one test, and restores the old
one through a shell trap.

**Critical hardware constraint:** Original Sanni uses a distinct
`write_reg_byte` for MMC1 $E000/$F000 to avoid accidental writes
into $6000-$7FFF PRG-WRAM. The Retrode3 kernel's `NES_REG`
mode does NOT provide a verified fast pulse on the required M2/ROMSEL
lines. The lab route instead invokes the normal PRG write pulse
only on a known board with **no PRG-WRAM**. Do NOT use on other MMC1
variants or promote it to general support. No electrically verified
MMC1 physical CRC exists yet. The expected Rad Racer PRG CRC is
`654F4E90` (PAL) or `8B9D3E9C` (US), depending on the version.

`test-native-nes-module` includes mock tests for no-ack refusal,
battery-layout refusal, exact serial write order, complete
128 KiB PRG CRC and failed-write cleanup. CI is set up on
`develop`; the full target-device compilation and physical testing
are **still pending**. The separately reviewed safe kernel
register-write implementation remains required for PRG-WRAM support.

**Never** use the general `RETRODE3_NES_MAPPER_WRITE_EXPERIMENTAL`
flag to enable MMC1. Only the dedicated `RETRODE3_NES_MMC1_LAB`
module has this narrowly scoped exception.

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
