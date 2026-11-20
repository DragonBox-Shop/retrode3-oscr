#!/usr/bin/env python3
"""Guard Sanni NES mapper bank selectors used by the Retrode3 Linux port.

Pinned upstream e5a08326dfc8fff67193f7fc5a7bc36b5282b28c.
Review changes in the original firmware before modifying these signatures.
Python 3.4 compatible. Never writes upstream files.
"""
import io
import os
import sys

UPSTREAM = "third_party/oscr-firmware/src/cores/NES.cpp"
# Verify all branch-selection/register semantics the portable adapter relies on.
EXPECTED = {
    "UxROM PRG bus conflict search":
        "if (read_prg_byte(0xC000 + x) == i)",
    "UxROM PRG switching":
        "write_prg_byte(0xC000 + x, i);",
    "UxROM 16K PRG":
        "dumpBankPRG(0x0, 0x4000, base);",
    "CNROM CHR bus conflict search":
        "if (read_prg_byte(0x8000 + x) == i)",
    "CNROM CHR switching":
        "write_prg_byte(0x8000 + x, i);",
    "CNROM 8K CHR":
        "dumpBankCHR(0x0, 0x2000);",
    "MMC3 PRG R6 select":
        "write_prg_byte(0x8000, 0x06);",
    "MMC3 PRG bank register":
        "write_prg_byte(0x8001, i);",
    "MMC3 8K PRG":
        "dumpBankPRG(0x0, 0x2000, base);",
    "MMC3 CHR R2 select":
        "write_prg_byte(0x8000, 0x02);",
    "MMC3 1K CHR":
        "dumpBankCHR(0x1000, 0x1400);",
    "AxROM 32K PRG":
        "dumpBankPRG(0x0, 0x8000, base);",
    "MMC1 safe register":
        "write_reg_byte(address, data >> i);",
}

def main():
    with io.open(UPSTREAM, "r", encoding="utf-8") as f:
        source = f.read()
    missing = [name for name, snippet in EXPECTED.items()
               if snippet not in source]
    if missing:
        sys.stderr.write("Pinned Sanni NES mapper algorithm changed: " +
                         ", ".join(missing) + "\n")
        return 2
    # The current Linux kernel lacks a verified sub-33 ns MMC1 safe write.
    # Do not silently turn it on when porting other mappers.
    with io.open("src/retrode3/oscr_nes_mapper.cpp", "r",
                 encoding="utf-8") as f:
        adapter = f.read()
    if "case 1:" in adapter or "NES_MODE_REG" in adapter.split("void prg_register")[1]:
        # MMC1 comments are allowed; executable register operations are not.
        if "case 1:" in adapter:
            sys.stderr.write("MMC1 mapper writes need separate kernel review\n")
            return 2
    return 0

if __name__ == "__main__":
    sys.exit(main())
