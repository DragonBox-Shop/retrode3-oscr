// Test-only hardware fixture. No actual cartridge, GPIO or write calls.
#include "retrode3-lib.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {
unsigned char prg[32768], chr[8192];
unsigned prg_size = 16384, chr_size = 8192;
bool initialized = false;
void init()
{
    if (initialized) return;
    initialized = true;
    char const *geometry = std::getenv("RETRODE_MOCK_NES_SIZE");
    unsigned multiplier = 7u, bias = 0u;
    // Each synthetic title must contain a DISTINCT PRG chip image.
    // Reusing identical PRG with and without CHR produces genuine
    // ambiguous OSCR records, which the production core rightly rejects.
    if (geometry && std::strcmp(geometry, "16-0") == 0) {
        chr_size = 0;
        multiplier = 9u;
        bias = 1u;
    }
    if (geometry && std::strcmp(geometry, "32-0") == 0) {
        prg_size = 32768;
        chr_size = 0;
        multiplier = 17u;
        bias = 3u;
    }
    if (geometry && std::strcmp(geometry, "32-8") == 0) {
        prg_size = 32768;
        multiplier = 13u;
        bias = 2u;
    }
    for (unsigned i = 0; i < sizeof(prg); ++i)
        prg[i] = static_cast<unsigned char>((i * multiplier + (i >> 8) + bias) & 255u);
    for (unsigned i = 0; i < sizeof(chr); ++i)
        chr[i] = static_cast<unsigned char>((i * 11u + 3u) & 255u);
    char const *bad = std::getenv("RETRODE_MOCK_NES_BAD");
    if (bad && std::strcmp(bad, "empty") == 0) {
        std::memset(prg, 0xff, sizeof(prg));
        std::memset(chr, 0xff, sizeof(chr));
    }
    if (bad && std::strcmp(bad, "unknown") == 0) {
        // Corrupt BOTH fingerprint windows so the mock remains unidentified.
        prg[0] ^= 0x5a;
        prg[0x2000] ^= 0x5a; // mirrored at CPU $E000 for 16 KiB NROM
        prg[0x6000] ^= 0x5a; // native $E000 window for 32 KiB NROM
    }
}
}
extern "C" int nes_read(std::uint16_t address, void *buf,
                        std::uint16_t size, int mode)
{
    init();
    if (!buf) return -1;
    char const *log = std::getenv("RETRODE_MOCK_NES_READLOG");
    if (log) {
        std::FILE *out = std::fopen(log, "a");
        if (out) {
            std::fprintf(out, "%u %u %d\n",
                         static_cast<unsigned>(address),
                         static_cast<unsigned>(size), mode);
            std::fclose(out);
        }
    }
    if (mode == NES_MODE_PRG) {
        if (address < 0x8000 || static_cast<unsigned>(address) + size > 0x10000u)
            return -1;
        for (unsigned n = 0; n < size; ++n)
            static_cast<unsigned char *>(buf)[n] =
                prg[(static_cast<unsigned>(address) - 0x8000u + n) % prg_size];
        return size;
    }
    if (mode == NES_MODE_CHR) {
        if (static_cast<unsigned>(address) + size > chr_size) return -1;
        std::memcpy(buf, chr + address, size);
        return size;
    }
    return -1;
}
extern "C" int nes_close(void) { return 0; }
extern "C" int md_read(std::uint32_t, void *, std::uint32_t, int) { return -1; }
extern "C" int snes_read(std::uint8_t, std::uint16_t, void *, std::uint16_t, int) { return -1; }
