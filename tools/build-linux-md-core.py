#!/usr/bin/env python3
"""Extract the pinned OSCR MegaDrive readWord and ROM-name helper for Linux.

Only the AVR register-level readWord body is replaced by the typed kernel
HAL. Original source signatures and copyToRomName implementation are retained.
This is a deliberately partial, read-only native stage, not a full MD port.
"""
import argparse
import io
import os
import re
import sys

WORD_START = "  uint16_t readWord(uint32_t myAddress)\n"
WORD_END = "  void writeFlash(uint32_t myAddress, uint16_t myData)\n"
COPY_START = "  uint8_t copyToRomName(char* output, uint8_t const * input, uint8_t length)\n"
COPY_END = "  void getCartInfo()\n"

# Exact AVR bus semantics of upstream e5a0832, with only comments/whitespace
# removed for comparison. Any control/timing change must be reviewed.
EXPECTED_WORD = """uint16_t readWord(uint32_t myAddress)
{
PORTF = myAddress & 0xFF;
PORTK = (myAddress >> 8) & 0xFF;
PORTL = (myAddress >> 16) & 0xFF;
NOP;
PORTH &= ~(1 << 3);
PORTH &= ~(1 << 6);
PORTJ &= ~(1 << 1);
PORTG &= ~(1 << 5);
pulse_clock(10);
NOP; NOP; NOP; NOP; NOP; NOP;
uint16_t tempWord = ((PINA & 0xFF) << 8) | (PINC & 0xFF);
PORTH |= (1 << 3);
PORTH |= (1 << 6);
PORTJ |= (1 << 1);
PORTG |= (1 << 5);
pulse_clock(10);
NOP; NOP; NOP; NOP; NOP; NOP;
return tempWord;
}"""


def canonical(text):
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"\s+", "", text)


def extract(source):
    for marker in (WORD_START, WORD_END, COPY_START, COPY_END):
        if source.count(marker) != 1:
            raise ValueError("Pinned upstream MegaDrive.cpp marker changed: " + marker.strip())
    word = source[source.index(WORD_START):source.index(WORD_END)]
    if canonical(word) != canonical(EXPECTED_WORD):
        raise ValueError("Original MegaDrive readWord GPIO/timing code changed; review required")
    helper = source[source.index(COPY_START):source.index(COPY_END)]
    if helper.count("uint8_t copyToRomName(") != 1 or re.search(
            r"\b(?:PORT[A-L]|PIN[A-L]|DDR[A-L]|NOP)\b", helper):
        raise ValueError("Original MegaDrive ROM-name helper changed")
    return ('// GENERATED FROM PINNED OSCR C++ MegaDrive.cpp. Do not edit.\n'
            '// Original method and naming helper retained; AVR bus replaced by Linux HAL.\n'
            '#include "retrode3/oscr_md_compat.h"\n'
            'namespace OSCR { namespace Cores { namespace MegaDrive {\n'
            '  uint16_t readWord(uint32_t myAddress)\n'
            '  {\n'
            '    return OSCR::RetroDe3::readWord(myAddress);\n'
            '  }\n\n'
            + helper + '\n}}}\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", default="third_party/oscr-firmware/src/cores/MegaDrive.cpp")
    parser.add_argument("--output", default="build/migration/oscr_md_core.cpp")
    args = parser.parse_args(argv)
    with io.open(args.upstream, "r", encoding="utf-8") as handle:
        generated = extract(handle.read())
    parent = os.path.dirname(os.path.abspath(args.output))
    if not os.path.isdir(parent):
        try:
            os.makedirs(parent)
        except OSError:
            if not os.path.isdir(parent):
                raise
    with io.open(args.output, "w", encoding="utf-8") as handle:
        handle.write(generated)
    print("Generated original OSCR MegaDrive readWord:", args.output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print("OSCR migration aborted: " + str(exc), file=sys.stderr)
        sys.exit(2)
