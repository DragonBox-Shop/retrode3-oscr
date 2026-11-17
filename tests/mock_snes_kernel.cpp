// Deterministic fake SNES kernel ABI for testing actual extracted OSCR C++
// bank-reader functions with zero access to physical cartridge hardware.
#include "retrode3-lib.h"
#include <cstdint>

extern "C" int nes_read(std::uint16_t, void *, std::uint16_t, int) { return -1; }
extern "C" int md_read(std::uint32_t, void *, std::uint32_t, int) { return -1; }
extern "C" int snes_close(void) { return 0; }
extern "C" int snes_read(std::uint8_t bank, std::uint16_t addr,
                          void *out, std::uint16_t size, int mode)
{
    if (mode != SNES_MODE_REGULAR || static_cast<unsigned>(addr) + size > 65536)
        return -1;
    // A deterministic byte pattern that depends on the physical bank
    // AND the address within it, exposing swapped/repeated banks.
    std::uint8_t *buffer = static_cast<std::uint8_t *>(out);
    for (unsigned i = 0; i < size; ++i)
        buffer[i] = static_cast<std::uint8_t>(
            (static_cast<unsigned>(bank) * 13u + ((static_cast<unsigned>(addr) + i) >> 8)) & 0xffu);
    return size;
}
