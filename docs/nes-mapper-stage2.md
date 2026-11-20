# NES mapper bank switching: develop-only prototype (2026-11-20)

The single `retrode3-cli` and `nes.so` now contain the first
Sanni-derived bank-switch sequencer. The original firmware submodule is
**unchanged**, pinned at `e5a08326dfc8fff67193f7fc5a7bc36b5282b28c`.
This is **not** a claim of electrical acceptance or production support.

## Mapper support levels

| Mapper | PRG/CHR strategy ported | Mock | Default production | Why |
| --- | --- | --- | --- | --- |
| 0 NROM | Linear 16/32K PRG, 0/8K CHR | Yes | Read-only enabled | Previous physical CRC; new v2 needs regression |
| 1 MMC1 | NOT enabled | Blocked | Blocked | Sanni's special $E000 register sequence needs ~33 ns transitions; kernel's NES_MODE_REG is not hardware-safe/verified |
| 2 UxROM | Switch 16K PRG at $8000, last 16K fixed | Yes | Blocked | Bus-conflict checks and CPU register-write timing await physical review |
| 3 CNROM | Fixed PRG, switch 8K CHR | Yes | Blocked | Bus-conflict checks and CPU register-write timing await physical review |
| 4 MMC3 | Switch 8K PRG using R6; 1K CHR using R2 | Yes | Blocked | Bank-write timing and MMC3 variants await hardware verification |
| 7 AxROM | Switch full 32K PRG at $8000 | Yes | Blocked | Register-write timing and mirroring variants await hardware verification |

**Never** enable MMC1 via the experimental flag; it has no executable
register-write path in the adapter. The mapper 2/3/4/7 code runs only in
`RETRODE3_TESTING` mock builds or a deliberately separate build with
`RETRODE3_NES_MAPPER_WRITE_EXPERIMENTAL`. That flag is **not** in the
standard Makefile/CI Debian staging options. Production
`get_capabilities` advertises those mappers with
`read_supported:false`; no hidden enablement on insertion.

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
