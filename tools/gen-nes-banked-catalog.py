#!/usr/bin/env python3
"""Generate pinned NES 2/3/4/7 metadata (Python 3.4 compatible)."""
import argparse
import hashlib
import io
import json

PINNED = "b07cfd2943b986f8ffc85e61722dbb2e440cec37"
INTRO = "// Generated from pinned Sanni NES database blob b07cfd2943b986f8ffc85e61722dbb2e440cec37.\n// Regenerate with tools/gen-nes-banked-catalog.py. Metadata only.\n"

def generate(data):
    if hashlib.sha1(("blob %d\0" % len(data)).encode("ascii") + data).hexdigest() != PINNED:
        raise ValueError("not the pinned Sanni NES database")
    records = [r for r in json.loads(data.decode("utf-8"))["records"]
               if r["mapper"] in (2, 3, 4, 7)]
    if len(records) != 1696:
        raise ValueError("unexpected catalogue count")
    lines = []
    for r in records:
        lines.append("    {%du, {0x%su,0x%su,0x%su,%du,%du,%du,%du,%s,%s}}," %
                     (r["mapper"], r.get("id32a") or "00000000",
                      r.get("id32b") or "00000000", r["crc32"],
                      r["iprg"] * 16, (r.get("ichr") or 0) * 4,
                      r.get("submapper") or 0, r.get("ram") or 0,
                      json.dumps(r["header"], ensure_ascii=False),
                      json.dumps(r["name"], ensure_ascii=False)))
    return INTRO + "\n".join(lines) + "\n"

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    with io.open(args.input, "rb") as src:
        content = generate(src.read())
    with io.open(args.output, "w", encoding="utf-8", newline="\n") as dst:
        dst.write(content)
