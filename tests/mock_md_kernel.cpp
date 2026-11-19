// Synthetic Retrode3 MD kernel ABI for the original upstream C++ word reader.
// Deliberately no cart writes, flash, mapper calls or real hardware access.
#include "retrode3-lib.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
unsigned char rom[524288];
bool initialized = false;
void init()
{
    if (initialized) return;
    initialized = true;
    for (unsigned i = 0; i < sizeof(rom); ++i)
        rom[i] = static_cast<unsigned char>((i * 7u + (i >> 8)) & 255u);
    std::memcpy(rom + 0x100, "SEGA", 4);
    std::memset(rom + 0x150, ' ', 48);
    std::memcpy(rom + 0x150, "MOCK MEGADRIVE CART", 19);
    std::memcpy(rom + 0x180, "GM MOCK0001-00", 14);
    std::memcpy(rom + 0x1f0, "JUE", 3);
    rom[0x18e] = 0x12; rom[0x18f] = 0x34;
    rom[0x1a4] = 0; rom[0x1a5] = 0x07; rom[0x1a6] = 0xff; rom[0x1a7] = 0xff;
    char const *bad = std::getenv("RETRODE_MOCK_MD_BAD");
    if (bad && std::strcmp(bad, "header") == 0) rom[0x100] = 'X';
    if (bad && std::strcmp(bad, "32x") == 0)
        std::memcpy(rom + 0x104, " 32X", 4);
    if (bad && std::strcmp(bad, "size") == 0)
        rom[0x1a5] = 0x70;
    if (bad && std::strcmp(bad, "svp") == 0)
        std::memcpy(rom + 0x180, "GM MK-1229 ", 11);
    if (bad && std::strcmp(bad, "lockon") == 0)
        std::memcpy(rom + 0x180, "GM MK-1563 -00", 14);
}
}

extern "C" int md_read(std::uint32_t addr, void *buffer, std::uint32_t count, int mode)
{
    init();
    if (mode != MD_MODE_P10 && mode != MD_MODE_ROM) return -1;
    if (addr > sizeof(rom) || count > sizeof(rom) - addr) return -1;
    std::memcpy(buffer, rom + addr, count);
    return static_cast<int>(count);
}
extern "C" int md_close(void) { return 0; }
extern "C" int nes_read(std::uint16_t, void *, std::uint16_t, int) { return -1; }
extern "C" int snes_read(std::uint8_t, std::uint16_t, void *, std::uint16_t, int) { return -1; }
