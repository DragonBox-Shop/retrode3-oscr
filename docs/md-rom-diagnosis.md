# MegaDrive ROM diagnosis: offline and read-only

This experiment does not change the cartridge or its contents. The original Sonic 2 cartridge is ROM-only; the discussion of accidental save/flash writes does not apply to it.

The existing MegaDrive acceptance test uses `MD_MODE_ROM` for bulk reads, matching the older Retrode3 OSCR adaptation; the old single-word helper uses `MD_MODE_P10`. Sanni's `md.json` contains Sonic 2 World CRC32 `24AB4C3A`, Rev A `7B905383` and Rev B `F23AD4B3`, but **not** `F0A4EBF0`. One MiB is a normal Sonic 2 size, so a CRC mismatch does not by itself indicate an overdump.

After `git pull --ff-only`, inspect the file that already exists:

```sh
python3 tools/md-rom-diagnose.py /tmp/retrode3-md-test.bin
```

Read `header_checksum_matches`, `size_matches_header`, `halves_identical`, `pair_swapped_crc32`, and the `64k_block_crc32` pattern. A valid internal checksum is supporting evidence only, not definitive CRC verification.

To compare two reads using the same mode, **use a new destination**:

```sh
python3 tools/retrode3-readtest.py --system md --action read-rom \
  --output /tmp/retrode3-md-repeat.bin
python3 tools/md-rom-diagnose.py /tmp/retrode3-md-test.bin \
  --compare /tmp/retrode3-md-repeat.bin
```

If reproducible but still not matching, compare the alternate kernel read mode without writing to the cartridge:

```sh
python3 tools/retrode3-readtest.py --system md --action read-rom \
  --md-mode p10 --output /tmp/retrode3-md-p10.bin
python3 tools/md-rom-diagnose.py /tmp/retrode3-md-test.bin \
  --compare /tmp/retrode3-md-p10.bin
```

Do not interpret different kernel mode dumps as necessarily incorrect until the kernel bus timing and address paths are checked.
