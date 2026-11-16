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

## Compare against your existing Sonic 2 reference ROM

The cartridge header `GM 00001051-01` identifies revision 01; the expected raw `.bin` reference CRC32 is `7B905383`. The header's `D951` checksum also matches the documented Sonic 2 ROM header. A `D962` checksum in our 1 MiB dump is therefore a real mismatch to investigate, not proof of an overdump.

Copy an unmodified **raw/deinterleaved** Sonic 2 Rev 01 `.bin` reference onto the Retrode3 (e.g. `/tmp/sonic2-reference.bin`) and run:

```sh
python3 tools/md-rom-diagnose.py /tmp/retrode3-md-test.bin \
  --compare /tmp/sonic2-reference.bin
```

The report shows `different_bytes`, the first 20 differing byte positions and values, and `different_bytes_by_64k_block` to reveal whether corruption is isolated or repeated. Do not compare against `.smd` interleaved files without converting them first. If the reference has another revision or is patched, differences do not automatically indicate a reader bug.

If the first read differs from the reference, repeat with a **new destination file** and compare against the first read. This separates intermittent bus/timing errors from reproducible distortions.

If reproducible but still not matching, compare the alternate kernel read mode without writing to the cartridge:

```sh
python3 tools/retrode3-readtest.py --system md --action read-rom \
  --md-mode p10 --output /tmp/retrode3-md-p10.bin
python3 tools/md-rom-diagnose.py /tmp/retrode3-md-test.bin \
  --compare /tmp/retrode3-md-p10.bin
```

Do not interpret different kernel mode dumps as necessarily incorrect until the kernel bus timing and address paths are checked.
