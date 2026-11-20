// Deterministic fake SNES kernel ABI for testing actual extracted OSCR C++
// bank-reader functions with zero access to physical cartridge hardware.
#include "retrode3-lib.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" int nes_read(std::uint16_t, void *, std::uint16_t, int) { return -1; }
extern "C" int md_read(std::uint32_t, void *, std::uint32_t, int) { return -1; }
extern "C" int snes_close(void) { return 0; }
extern "C" int snes_read(std::uint8_t bank, std::uint16_t addr,
                          void *out, std::uint16_t size, int mode)
{
    if (mode != SNES_MODE_REGULAR || static_cast<unsigned>(addr) + size > 65536)
        return -1;
    if (bank == 0 && addr == 0xffb0 && size == 80) {
        // Synthetic conventional 512-KiB SNES header for the ORIGINAL
        // OSCR checkcart() code. Bulk ROM reads still get bank patterns.
        std::uint8_t header[80] = {};
        std::memcpy(header + 0x10, "MOCK SNES CART", 14);
        char const *map = std::getenv("RETRODE_MOCK_MAP");
        header[0x25] = (map && std::strcmp(map, "hi") == 0) ? 0x21 : 0x20;
        header[0x26] = std::getenv("RETRODE_MOCK_SNES_SPECIAL") ? 4 : 0;
        header[0x27] = 9; // 2^(9+10) bytes = 512 KiB
        header[0x2c] = 0xcb;
        header[0x2d] = 0xed; // complement 0xEDCB
        header[0x2e] = 0x34;
        header[0x2f] = 0x12; // checksum 0x1234
        std::memcpy(out, header, sizeof(header));
        return size;
    }
    // A deterministic byte pattern that depends on the physical bank
    // AND the address within it, exposing swapped/repeated banks.
    std::uint8_t *buffer = static_cast<std::uint8_t *>(out);
    for (unsigned i = 0; i < size; ++i)
        buffer[i] = static_cast<std::uint8_t>(
            (static_cast<unsigned>(bank) * 13u + ((static_cast<unsigned>(addr) + i) >> 8)) & 0xffu);
    return size;
}
