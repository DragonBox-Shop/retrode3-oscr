// Test-only hardware fixture. No actual cartridge, GPIO or write calls.
#include "retrode3-lib.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
unsigned char prg[16384], chr[8192];
bool initialized = false;
void init()
{
    if (initialized) return;
    initialized = true;
    for (unsigned i = 0; i < sizeof(prg); ++i)
        prg[i] = static_cast<unsigned char>((i * 7u + (i >> 8)) & 255u);
    for (unsigned i = 0; i < sizeof(chr); ++i)
        chr[i] = static_cast<unsigned char>((i * 11u + 3u) & 255u);
    char const *bad = std::getenv("RETRODE_MOCK_NES_BAD");
    if (bad && std::strcmp(bad, "empty") == 0) {
        std::memset(prg, 0xff, sizeof(prg));
        std::memset(chr, 0xff, sizeof(chr));
    }
    if (bad && std::strcmp(bad, "unknown") == 0) prg[0] ^= 0x5a;
}
}
extern "C" int nes_read(std::uint16_t address, void *buf,
                        std::uint16_t size, int mode)
{
    init();
    if (!buf) return -1;
    if (mode == NES_MODE_PRG) {
        if (address < 0x8000 || static_cast<unsigned>(address) + size > 0x10000u)
            return -1;
        for (unsigned n = 0; n < size; ++n)
            static_cast<unsigned char *>(buf)[n] =
                prg[(static_cast<unsigned>(address) - 0x8000u + n) % sizeof(prg)];
        return size;
    }
    if (mode == NES_MODE_CHR) {
        if (static_cast<unsigned>(address) + size > sizeof(chr)) return -1;
        std::memcpy(buf, chr + address, size);
        return size;
    }
    return -1;
}
extern "C" int nes_close(void) { return 0; }
extern "C" int md_read(std::uint32_t, void *, std::uint32_t, int) { return -1; }
extern "C" int snes_read(std::uint8_t, std::uint16_t, void *, std::uint16_t, int) { return -1; }
