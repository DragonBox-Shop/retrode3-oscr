#!/usr/bin/env python3
"""Deterministically reuse pinned OSCR SNES C++ bank-reader functions on Linux.

This is the first *partial* native port, not the complete SNES core. Only the
AVR GPIO read loops are replaced. The original submodule remains pristine.
Unrecognized upstream source changes fail the build instead of being guessed.
"""
import argparse
from pathlib import Path
import re
import sys

START = "  void readLoRomBanks(uint16_t start, uint16_t total, OSCR::Storage::File * file)\n"
HI = "  void readHiRomBanks(uint16_t start, uint16_t total, OSCR::Storage::File * file)\n"
END = "  /******************************************\n    SNES ROM Functions"
LOOP_HEAD = "        for (uint16_t c = 0; c < 1024; c++, currByte++)\n"
EXPECTED_GPIO = """for (uint16_t c = 0; c < 1024; c++, currByte++) {
PORTF = (currByte & 0xFF);
PORTK = ((currByte >> 8) & 0xFF);
NOP; NOP; NOP; NOP; NOP; NOP;
buffer[c] = PINC;
}"""


def canonical(code):
    code = re.sub(r"//[^\n]*", "", code)
    return re.sub(r"\s+", "", code)


def extract(source):
    for marker in (START, HI, END):
        if source.count(marker) != 1:
            raise ValueError("Pinned upstream SNES.cpp changed marker: " + marker[:60])
    first = source.index(START)
    final = source.index(END, first)
    selected = source[first:final]
    if selected.count("      PORTL = currBank;") != 2 or selected.count(LOOP_HEAD) != 2:
        raise ValueError("Pinned upstream SNES bank readers have changed")
    selected = selected.replace("      PORTL = currBank;",
                                "      // The Linux readBlock HAL selects this bank.")
    for _ in range(2):
        first = selected.index(LOOP_HEAD)
        final = selected.index("        file->write(buffer, 1024);", first)
        old = selected[first:final]
        if canonical(old) != canonical(EXPECTED_GPIO):
            raise ValueError("Upstream AVR SNES byte loop changed; review required")
        replacement = ("        OSCR::RetroDe3::readBlock(static_cast<uint8_t>(currBank), "
                       "currByte, buffer, sizeof(buffer));\n"
                       "        currByte = static_cast<uint16_t>(currByte + sizeof(buffer));\n\n")
        selected = selected[:first] + replacement + selected[final:]
    if re.search(r"\b(?:PORT[A-Z]|PIN[A-Z]|DDR[A-Z]|NOP)\b", selected):
        raise ValueError("Unexpected direct AVR access remains in SNES bank reader")
    if selected.count("void readLoRomBanks(") != 1 or selected.count("void readHiRomBanks(") != 1:
        raise ValueError("Missing original OSCR function")
    return ('// GENERATED FROM PINNED OSCR C++ SNES.cpp. Do not edit.\n'
            '// Original banking loops retained; AVR byte reads use kernel HAL.\n'
            '#include "retrode3/oscr_snes_compat.h"\n'
            'namespace OSCR { namespace Cores { namespace SNES {\n'
            + selected + '\n}}}\n')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--upstream", default="third_party/oscr-firmware/src/cores/SNES.cpp")
    p.add_argument("--output", default="build/migration/oscr_snes_banks.cpp")
    args = p.parse_args(argv)
    src = Path(args.upstream).read_text(encoding="utf-8")
    target = Path(args.output)
    native = extract(src)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(native, encoding="utf-8")
    print("Generated upstream OSCR C++ bank readers:", str(target))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print("OSCR migration aborted: " + str(exc), file=sys.stderr)
        sys.exit(2)
