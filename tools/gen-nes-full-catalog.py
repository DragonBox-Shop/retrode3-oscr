#!/usr/bin/env python3
"""Regenerate all pinned Sanni NES catalogue partitions, Python 3.4+.

ROM size is decoded from the original header, not the upstream rounded
bank-exponent fields. Existing candidate ordering is retained. The original
226 NROM rows and synthetic mock rows remain in oscr_nes_catalog.cpp.
No network access during a normal core build.
"""
import argparse
import hashlib
import io
import json
import os
import re

PINNED = "b07cfd2943b986f8ffc85e61722dbb2e440cec37"
INTRO = ("// Generated from pinned Sanni NES database blob " + PINNED + ".\n"
         "// Regenerate with tools/gen-nes-full-catalog.py. Metadata only.\n")

def rom_kib(low, high, unit):
    if high == 15:
        size = (1 << (low >> 2)) * ((low & 3) * 2 + 1)
        if size % 1024:
            raise ValueError("non-KiB NES ROM size")
        return size // 1024
    return (low + (high << 8)) * unit

def dimensions(record):
    header = bytearray.fromhex(record["header"])
    if len(header) != 16 or header[:4] != bytearray(b"NES\x1a"):
        raise ValueError("invalid NES header")
    nes2 = header[7] & 12 == 8
    high = header[9] if nes2 else 0
    mapper = (header[6] >> 4) | (header[7] & 240)
    if nes2:
        mapper |= (header[8] & 15) << 8
    if mapper != record["mapper"] or (header[8] >> 4 if nes2 else 0) != (record.get("submapper") or 0):
        raise ValueError("header mapper mismatch")
    prg, chr_ = rom_kib(header[4], high & 15, 16), rom_kib(header[5], high >> 4, 8)
    if max(prg, chr_) > 0xffffffff:
        raise ValueError("NES ROM size overflow")
    return prg, chr_

def standard_nrom(record):
    prg, chr_ = dimensions(record)
    return (record["mapper"] == 0 and not record.get("submapper") and
            prg in (16, 32) and chr_ in (0, 8) and
            not (int(record["header"][12:14], 16) & 4))

def q(value):
    return json.dumps(value, ensure_ascii=False)

def crc(record, key):
    value = record.get(key) or "00000000"
    if not re.match(r"^[0-9A-Fa-f]{8}$", value):
        raise ValueError("invalid NES CRC")
    return "0x" + value + "u"

def metadata(record):
    prg, chr_ = dimensions(record)
    return "%s,%s,%s,%du,%du,%du,%du,%s,%s" % (
        crc(record, "id32a"), crc(record, "id32b"), crc(record, "crc32"),
        prg, chr_, record.get("submapper") or 0, record.get("ram") or 0,
        q(record["header"]), q(record["name"]))

def banked_row(record):
    return "    {%du, {%s}}," % (record["mapper"], metadata(record))

def nrom_row(record):
    prg, chr_ = dimensions(record)
    return "    {%s,%s,%s,%du,%du,%s,%s}," % (
        crc(record, "id32a"), crc(record, "id32b"), crc(record, "crc32"),
        prg, chr_, q(record["header"]), q(record["name"]))

def load(data):
    if hashlib.sha1(("blob %d\0" % len(data)).encode("ascii") + data).hexdigest() != PINNED:
        raise ValueError("not the pinned Sanni NES database")
    records = json.loads(data.decode("utf-8"))["records"]
    if len(records) != 5562 or len(set(r["crc32"] for r in records)) != len(records):
        raise ValueError("unexpected NES catalogue record count or duplicate CRC")
    for record in records:
        dimensions(record)
    return records

def mmc1(records):
    rows = [r for r in records if r["mapper"] == 1]
    if len(rows) != 695:
        raise ValueError("unexpected MMC1 count")
    return (INTRO + '#include "retrode3/oscr_nes_catalog.h"\n'
            'namespace retrode3 { namespace nesstage1 {\n'
            'Mmc1Record const mmc1_records[] = {\n' +
            "\n".join("    {%s}," % metadata(r) for r in rows) +
            "\n};\nstd::size_t const mmc1_record_count = sizeof(mmc1_records) / sizeof(mmc1_records[0]);\n} }\n")

def banked(records):
    rows = [r for r in records if r["mapper"] in (2, 3, 4, 7)]
    if len(rows) != 1696:
        raise ValueError("unexpected existing banked count")
    return INTRO + "\n".join(banked_row(r) for r in rows) + "\n"

def generate(data, directory):
    records = load(data)
    with io.open(os.path.join(directory, "oscr_nes_catalog.cpp"), encoding="utf-8") as handle:
        old = handle.read().split("#ifdef RETRODE3_TESTING")[0]
    legacy = re.findall(r"\{0x[0-9a-f]+u,0x[0-9a-f]+u,0x([0-9a-f]+)u,", old, re.I)
    legacy = set(x.upper() for x in legacy)
    expected = set(r["crc32"] for r in records if standard_nrom(r) and
                   (r.get("id32a") or r.get("id32b")))
    if len(legacy) != 226 or legacy != expected:
        raise ValueError("legacy NROM identity partition changed")
    nrom = [r for r in records if standard_nrom(r) and r["crc32"] not in legacy]
    other = [r for r in records if r["mapper"] not in (1, 2, 3, 4, 7)
             and not standard_nrom(r)]
    if len(legacy) + len(nrom) + len(other) + 695 + 1696 != len(records):
        raise ValueError("incomplete NES catalogue")
    return {
        "oscr_nes_mmc1_catalog.cpp": mmc1(records),
        "oscr_nes_banked_catalog.inc": banked(records),
        "oscr_nes_extra_nrom_catalog.inc": INTRO + "\n".join(nrom_row(r) for r in nrom) + "\n",
        "oscr_nes_extra_banked_catalog.inc": INTRO + "\n".join(banked_row(r) for r in other) + "\n",
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--directory", default="src/retrode3")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    with io.open(args.input, "rb") as source:
        output = generate(source.read(), args.directory)
    for name, content in sorted(output.items()):
        path = os.path.join(args.directory, name)
        if args.check:
            with io.open(path, encoding="utf-8") as handle:
                if handle.read() != content:
                    raise ValueError("generated catalogue differs: " + path)
        else:
            with io.open(path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(content)
    print("NES catalogue: 5562 pinned records, 269 mappers")

if __name__ == "__main__":
    main()
