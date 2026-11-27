#!/usr/bin/env python3
"""Verify complete upstream mapper extraction and strict patch application."""
import importlib.machinery
import importlib.util
import io
import os
import re
import sys

UPSTREAM = "third_party/oscr-firmware/src/cores/NES.cpp"
def main():
    loader = importlib.machinery.SourceFileLoader("nes_generator", "tools/build-linux-nes-core.py")
    # Python 3.4 supports load_module; avoid requiring newer importlib APIs.
    generator = loader.load_module()
    with io.open(UPSTREAM, "r", encoding="utf-8") as f:
        source = f.read()
    generated = generator.extract(source)
    raw = generator.mapper_source(source)
    for indent, count in ((6, 195), (4, 142)):
        pattern = r"^" + " " * indent + r"case (\d+):"
        # PRG and CHR top-level switch indent differs; split the functions.
        tag = "void generatedReadPRG" if indent == 6 else "void generatedReadCHR"
        stop = "void generatedReadCHR" if indent == 6 else None
        a = raw.index(tag)
        b = raw.index(stop, a) if stop else len(raw)
        c = generated.index(tag)
        d = generated.index(stop, c) if stop else len(generated)
        cases = re.findall(pattern, raw[a:b], re.M)
        assert len(set(cases)) == count, (tag, len(set(cases)))
        assert cases == re.findall(pattern, generated[c:d], re.M), tag
    assert "write_mmc1_byte(0x8000, 0x0C);" in generated
    assert "write_mmc1_byte(0xA000, i > 15 ? 0x10 : 0);" in generated
    # Deliberately break a patch anchor; the generator must reject it.
    with io.open("patches/nes-generated.json", "r", encoding="utf-8") as f:
        import json
        patch = json.load(f)[0]
    try:
        generator.apply_patches(raw.replace(patch["before"], ""), "patches/nes-generated.json")
    except ValueError:
        pass
    else:
        raise AssertionError("unmatched patch accepted")
    print("NES upstream mapper switches: 195 PRG / 142 CHR; strict patches OK")
    return 0

if __name__ == "__main__":
    sys.exit(main())
