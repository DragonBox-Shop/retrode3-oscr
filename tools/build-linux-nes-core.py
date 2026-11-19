#!/usr/bin/env python3
"""Strictly extract original pinned Sanni NES NROM bank iteration for Linux.

The original dumpBankPRG/dumpBankCHR loops are preserved verbatim.
Original dumpPRG/dumpCHR per-byte AVR reading is reviewed and substituted
with one 512-byte typed Linux read-only bus transaction per buffer.
Reject any unexpected source algorithm change before building. Python 3.4.
"""
import argparse
import io
import os
import re
import sys

MARKERS = (
    ("dumpPRG", "  void dumpPRG(uint16_t base, uint16_t address)\n",
     "  void dumpCHR(uint16_t address)\n"),
    ("dumpCHR", "  void dumpCHR(uint16_t address)\n",
     "  void dumpCHR_M2(uint16_t address)\n"),
    ("dumpBankPRG", "  void dumpBankPRG(size_t const from, size_t const to, size_t const base)\n",
     "  void dumpBankCHR(size_t const from, size_t const to)\n"),
    ("dumpBankCHR", "  void dumpBankCHR(size_t const from, size_t const to)\n",
     "  void readPRG(bool readrom)\n"),
)
EXPECTED = {
    "dumpPRG": """void dumpPRG(uint16_t base, uint16_t address) {
        for (size_t x = 0; x < 512; x++) {
            OSCR::Storage::Shared::buffer[x] = read_prg_byte(base + address + x);
        }
        OSCR::Storage::Shared::writeBuffer();
        OSCR::UI::ProgressBar::advance(512);
    }""",
    "dumpCHR": """void dumpCHR(uint16_t address) {
        for (size_t x = 0; x < 512; x++) {
            OSCR::Storage::Shared::buffer[x] = read_chr_byte(address + x);
        }
        OSCR::Storage::Shared::writeBuffer();
        OSCR::UI::ProgressBar::advance(512);
    }""",
    "dumpBankPRG": """void dumpBankPRG(size_t const from, size_t const to, size_t const base) {
        for (size_t address = from; address < to; address += 512) {
            dumpPRG(base, address);
        }
    }""",
    "dumpBankCHR": """void dumpBankCHR(size_t const from, size_t const to) {
        for (size_t address = from; address < to; address += 512) {
            dumpCHR(address);
        }
    }""",
}

def canonical(s):
    s = re.sub(r"//[^\n]*", "", s)
    return re.sub(r"\s+", "", s)

def extract(source):
    chunks = {}
    for name, begin, end in MARKERS:
        if source.count(begin) != 1 or source.count(end) != 1:
            raise ValueError("original OSCR NES method marker changed: " + name)
        a = source.index(begin)
        b = source.index(end, a + len(begin))
        chunk = source[a:b]
        if canonical(chunk) != canonical(EXPECTED[name]):
            raise ValueError("original OSCR NES " + name + " changed; review required")
        chunks[name] = chunk

    # Replace ONLY the AVR per-byte loops. Original upstream bank ordering
    # stays verbatim and uses the same 512-byte output buffer.
    prg = """  void dumpPRG(uint16_t base, uint16_t address) {
        OSCR::RetroDe3::readPRGBlock(
            static_cast<uint16_t>(base + address),
            OSCR::Storage::Shared::buffer, 512);
        OSCR::Storage::Shared::writeBuffer();
        OSCR::UI::ProgressBar::advance(512);
    }

"""
    chr = """  void dumpCHR(uint16_t address) {
        OSCR::RetroDe3::readCHRBlock(
            address, OSCR::Storage::Shared::buffer, 512);
        OSCR::Storage::Shared::writeBuffer();
        OSCR::UI::ProgressBar::advance(512);
    }

"""
    return ('// AUTO-GENERATED from PINNED original OSCR NES.cpp. DO NOT EDIT.\n'
            '// Only AVR per-byte reads converted to 512-byte Linux HAL blocks.\n'
            '#include "retrode3/oscr_nes_compat.h"\n'
            'namespace OSCR { namespace Cores { namespace NES {\n'
            + prg + chr + chunks["dumpBankPRG"] + chunks["dumpBankCHR"]
            + '} } }\n')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--upstream", default="third_party/oscr-firmware/src/cores/NES.cpp")
    ap.add_argument("--output", default="build/migration/oscr_nes_nrom.cpp")
    args = ap.parse_args()
    with io.open(args.upstream, "r", encoding="utf-8") as handle:
        generated = extract(handle.read())
    parent = os.path.dirname(args.output)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    with io.open(args.output, "w", encoding="utf-8") as handle:
        handle.write(generated)

if __name__ == "__main__":
    try:
        main()
    except (ValueError, IOError) as exc:
        sys.stderr.write(str(exc) + "\n")
        sys.exit(2)
