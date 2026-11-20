#!/usr/bin/env python3
"""Regenerate mapper-1 metadata from pinned oscartreader/oscr-databases NES JSON.

Use the exact file from commit 87a48948c6a04260205f148d313614cc44e05cd5.
No source ROM bytes, mapper guesses or silent database updates.
Python 3.4 compatible. Explicit invocation only, never network at build time.
"""
from __future__ import print_function
import argparse
import hashlib
import io
import json
import re

PINNED_BLOB = "b07cfd2943b986f8ffc85e61722dbb2e440cec37"
INPUT_COMMIT = "87a48948c6a04260205f148d313614cc44e05cd5"


def git_blob(data):
    header = ("blob %d\0" % len(data)).encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def q(s):
    return json.dumps(s, ensure_ascii=False)


def generate(data):
    if git_blob(data) != PINNED_BLOB:
        raise ValueError("NES DB is not the exact pinned Sanni source blob")
    entries = json.loads(data.decode("utf-8"))["records"]
    records = [r for r in entries if r.get("mapper") == 1]
    if len(records) != 695:
        raise ValueError("pinned MMC1 catalogue count changed")
    lines = []
    for r in records:
        prg = r["iprg"]
        chr_banks = r.get("ichr") or 0
        header = r["header"]
        if prg not in (2, 4, 8, 16, 32) or chr_banks not in (
                0, 1, 2, 4, 8, 16, 32):
            raise ValueError("unexpected mapper-1 PRG/CHR geometry")
        if (not re.match(r"^[0-9A-Fa-f]{32}$", header) or
                not re.match(r"^[0-9A-Fa-f]{8}$", r["crc32"])):
            raise ValueError("unexpected mapper-1 header or CRC")
        lines.append("    {0x%su,0x%su,0x%su,%d,%d,%d,%d,%s,%s}," %
                     (r.get("id32a") or "00000000",
                      r.get("id32b") or "00000000",
                      r["crc32"], prg * 16, chr_banks * 8,
                      r.get("submapper") or 0, r.get("ram") or 0,
                      q(header), q(r["name"])))
    return """// Auto-generated pinned Sanni OSCR NES mapper-1 metadata ONLY.
// Source: oscartreader/oscr-databases data/nes.json
// Pinned commit: 87a48948c6a04260205f148d313614cc44e05cd5
// Source blob: b07cfd2943b986f8ffc85e61722dbb2e440cec37
// DO NOT edit manually; regenerate using tools/gen-nes-mmc1-catalog.py.
// Metadata only, never copyrighted ROM bytes. Keep records without
// fingerprints for post-dump payload CRC verification. Unsupported MMC1
// variants appear as read_supported:false in the fast-probe output.
#include "retrode3/oscr_nes_catalog.h"
namespace retrode3 { namespace nesstage1 {
Mmc1Record const mmc1_records[] = {
%s
};
std::size_t const mmc1_record_count = sizeof(mmc1_records) / sizeof(mmc1_records[0]);
} }
""" % "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    with io.open(args.input, "rb") as source:
        out = generate(source.read())
    with io.open(args.output, "w", encoding="utf-8", newline="\n") as dest:
        dest.write(out)


if __name__ == "__main__":
    main()
