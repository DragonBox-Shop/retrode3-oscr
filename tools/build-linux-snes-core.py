#!/usr/bin/env python3
"""Deterministically reuse pinned OSCR SNES C++ bank-reader functions on Linux.

This is the first *partial* native port, not the complete SNES core. Only the
AVR GPIO read loops are replaced. The original submodule remains pristine.
Unrecognized upstream source changes fail the build instead of being guessed.
"""
import argparse
import io
import os
import re
import sys

START = "  void readLoRomBanks(uint16_t start, uint16_t total, OSCR::Storage::File * file)\n"
HI = "  void readHiRomBanks(uint16_t start, uint16_t total, OSCR::Storage::File * file)\n"
END = "  /******************************************\n    SNES ROM Functions"
LOOP_HEAD = "        for (uint16_t c = 0; c < 1024; c++, currByte++)\n"
HEADER_START = "  bool checkcart()\n"
HEADER_END = "  uint16_t calc_checksum()\n"
EXPECTED_GPIO = """for (uint16_t c = 0; c < 1024; c++, currByte++) {
PORTF = (currByte & 0xFF);
PORTK = ((currByte >> 8) & 0xFF);
NOP; NOP; NOP; NOP; NOP; NOP;
buffer[c] = PINC;
}"""


def canonical(code):
    code = re.sub(r"//[^\n]*", "", code)
    return re.sub(r"\s+", "", code)


# Only AVR-specific header I/O is replaced. All mapping/size/checksum
# decisions are retained from the original upstream C++ checkcart().
EXPECTED_HEADER_GPIO = """dataIn();
PORTL = 0;
for (uint16_t c = 0, currByte = headerStart; c < 80; c++, currByte++) {
PORTF = (currByte & 0xFF);
PORTK = ((currByte >> 8) & 0xFF);
NOP; NOP; NOP; NOP; NOP; NOP;
NOP; NOP; NOP; NOP; NOP; NOP;
snesHeader[c] = PINC;
}"""


def extract(source):
    for marker in (START, HI, END, HEADER_START, HEADER_END):
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
    start = source.index(HEADER_START)
    finish = source.index(HEADER_END, start)
    header = source[start:finish]
    gpio_start = header.index("    // set control to read")
    gpio_end = header.index("    // Calculate CRC32 of header", gpio_start)
    original_gpio = header[gpio_start:gpio_end]
    if canonical(original_gpio) != canonical(EXPECTED_HEADER_GPIO):
        raise ValueError("Upstream checkcart() AVR header-read logic changed")
    header = (header[:gpio_start]
              + "    OSCR::RetroDe3::readBlock(0, headerStart, snesHeader, sizeof(snesHeader));\n\n"
              + header[gpio_end:])
    # OSCR's fallback game-code char[5] lacks a terminator on the AVR.
    # Initialize this local buffer for safe host C++ snprintf().
    if header.count("      char code[5];") != 1:
        raise ValueError("Original OSCR fallback game-code buffer changed")
    header = header.replace("      char code[5];", "      char code[5] = {};")
    if re.search(r"\b(?:PORT[A-Z]|PIN[A-Z]|DDR[A-Z]|NOP)\b", header):
        raise ValueError("Unexpected AVR GPIO access in OSCR checkcart()")
    return ('// GENERATED FROM PINNED OSCR C++ SNES.cpp. Do not edit.\n'
            '// Original banking loops retained; AVR byte reads use kernel HAL.\n'
            '#include "retrode3/oscr_snes_compat.h"\n'
            'namespace OSCR { namespace Cores { namespace SNES {\n'
            + selected + '\n' + header + '\n}}}\n')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--upstream", default="third_party/oscr-firmware/src/cores/SNES.cpp")
    p.add_argument("--output", default="build/migration/oscr_snes_banks.cpp")
    args = p.parse_args(argv)
    # pathlib.Path.read_text/write_text and mkdir(exist_ok=...) are not
    # available on the Retrode3's Python 3.4 (Debian 8).
    with io.open(args.upstream, "r", encoding="utf-8") as source_file:
        src = source_file.read()
    native = extract(src)
    parent = os.path.dirname(os.path.abspath(args.output))
    if not os.path.isdir(parent):
        try:
            os.makedirs(parent)
        except OSError:
            if not os.path.isdir(parent):
                raise
    with io.open(args.output, "w", encoding="utf-8") as generated_file:
        generated_file.write(native)
    print("Generated upstream OSCR C++ bank readers:", args.output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print("OSCR migration aborted: " + str(exc), file=sys.stderr)
        sys.exit(2)
