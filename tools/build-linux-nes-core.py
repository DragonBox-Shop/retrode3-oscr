#!/usr/bin/env python3
"""Generate original pinned Sanni NES ROM algorithms for Linux.

The original dumpBankPRG/dumpBankCHR loops are preserved verbatim.
Original dumpPRG/dumpCHR per-byte AVR reading is reviewed and substituted
with one 512-byte typed Linux read-only bus transaction per buffer.
Reject any unexpected source algorithm change before building. Python 3.4.
"""
import argparse
import io
import json
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


# Extract the original algorithm bodies, not a local list of mapper cases.
# All top-level cases are emitted; execution approval belongs to the HAL.
def section(source, begin, end):
    if source.count(begin) != 1 or source.count(end) != 1:
        raise ValueError("NES upstream section marker changed: " + begin)
    a = source.index(begin)
    b = source.index(end, a + len(begin))
    return source[a:b]

def mapper_switch(source, begin, end):
    body = section(source, begin, end)
    start = "    switch (NES_MAPPER)\n    {\n"
    if body.count(start) != 1:
        raise ValueError("NES upstream mapper switch marker changed")
    body = body[body.index(start):]
    finish = "\n    }\n\n    if (!readrom)"
    if body.count(finish) != 1:
        raise ValueError("NES upstream mapper switch end changed")
    return body[:body.index(finish)] + "\n    }\n"

def mapper_source(source):
    prg = mapper_switch(source, "  void readPRG(bool readrom)\n",
                        "  void readCHR(bool readrom)\n")
    chr_code = mapper_switch(source, "  void readCHR(bool readrom)\n",
                             "  void readRAM()\n")
    serial = section(source, "  void write_mmc1_byte(uint16_t address, uint8_t data)\n",
                     "  // REFERENCE FOR REGISTER WRITE TO 0xE000/0xF000\n")
    return (serial +
            "  void generatedReadPRG(unsigned NES_MAPPER, unsigned NES_PRG) {\n"
            "    const uint32_t oldcrc32 = 0, oldcrc32MMC3 = 0;\n"
            "    uint16_t base = 0x8000;\n"
            "    bool busConflict = false;\n"
            "    uint16_t banks;\n" + prg + "  }\n"
            "  void generatedReadCHR(unsigned NES_MAPPER, unsigned NES_CHR) {\n"
            "    uint16_t banks;\n"
            "    bool busConflict = false;\n" + chr_code + "  }\n")

def apply_patches(code, path):
    with io.open(path, "r", encoding="utf-8") as handle:
        patches = json.load(handle)
    for patch in patches:
        before, after = patch["before"], patch["after"]
        if code.count(before) != 1:
            raise ValueError("NES patch not uniquely applicable: " + patch["reason"])
        code = code.replace(before, after, 1)
    return code

def extract(source, patch_path=None):
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
    generated = ('// AUTO-GENERATED from PINNED original OSCR NES.cpp. DO NOT EDIT.\n'
            '// Original mapper switches and MMC1 serial loop; Linux HAL and reviewed patches.\n'
            '#include "retrode3/oscr_nes_compat.h"\n'
            '#define F(x) x\n'
            'namespace OSCR { namespace Cores { namespace NES {\n'
            + prg + chr + chunks["dumpBankPRG"] + chunks["dumpBankCHR"]
            + mapper_source(source) + '} } }\n')
    if patch_path is None:
        patch_path = os.path.join(os.path.dirname(__file__), '..', 'patches', 'nes-generated.json')
    return apply_patches(generated, patch_path)

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
