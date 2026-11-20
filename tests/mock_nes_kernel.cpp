// Test-only NES bus emulator. No real GPIO or cartridge writes.
#include "retrode3-lib.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {
unsigned char prg[32768], chr[8192];
unsigned prg_size = 16384, chr_size = 8192;
unsigned mapper = 0, ux_bank = 0, cn_bank = 0, ax_bank = 0;
unsigned mmc_mode = 0, mmc_prg_bank = 0, mmc_chr_bank = 0;
std::vector<unsigned char> rom_prg, rom_chr;
bool initialized = false;
unsigned char pattern_prg(unsigned i)
{
    return static_cast<unsigned char>((i * 7u + (i >> 8) * 11u +
                                      (i >> 13) * 33u + 13u) & 255u);
}
unsigned char pattern_chr(unsigned i)
{
    return static_cast<unsigned char>((i * 5u + (i >> 8) * 17u +
                                      (i >> 10) * 41u + 9u) & 255u);
}
void init()
{
    if (initialized) return;
    initialized = true;
    const char *m = std::getenv("RETRODE_MOCK_NES_MAPPER");
    if (m) mapper = static_cast<unsigned>(std::atoi(m));
    if (mapper) {
        prg_size = mapper == 2 ? 65536u :
                   mapper == 3 ? 32768u : 131072u;
        chr_size = mapper == 3 ? 32768u :
                   mapper == 4 ? 65536u : 0u;
        rom_prg.resize(prg_size);
        rom_chr.resize(chr_size);
        for (unsigned i = 0; i < prg_size; ++i) rom_prg[i] = pattern_prg(i);
        for (unsigned i = 0; i < chr_size; ++i) rom_chr[i] = pattern_chr(i);
        const char *no_safe = std::getenv("RETRODE_MOCK_NES_NO_CONFLICT_SAFE");
        if (no_safe && mapper == 2)
            for (unsigned i = prg_size - 16384u; i < prg_size; ++i)
                rom_prg[i] = 0xff;
        if (no_safe && mapper == 3) {
            for (unsigned i = 0; i < prg_size; ++i) rom_prg[i] = 0xff;
            // Avoid "empty slot" fast-probe while offering no safe 0..3 value.
            rom_prg[0] = 0xfe;
            rom_prg[0x6000] = 0xfe;
        }
        return;
    }
    char const *geometry = std::getenv("RETRODE_MOCK_NES_SIZE");
    unsigned multiplier = 7u, bias = 0u;
    if (geometry && std::strcmp(geometry, "16-0") == 0) {
        chr_size = 0; multiplier = 9u; bias = 1u;
    }
    if (geometry && std::strcmp(geometry, "32-0") == 0) {
        prg_size = 32768; chr_size = 0; multiplier = 17u; bias = 3u;
    }
    if (geometry && std::strcmp(geometry, "32-8") == 0) {
        prg_size = 32768; multiplier = 13u; bias = 2u;
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
        prg[0] ^= 0x5a;
        prg[0x2000] ^= 0x5a;
        prg[0x6000] ^= 0x5a;
    }
}
unsigned char mapped_prg(unsigned address)
{
    unsigned offset = address - 0x8000u;
    if (mapper == 2) {
        unsigned bank = address < 0xc000u ? ux_bank : prg_size / 16384u - 1u;
        return rom_prg[(bank * 16384u + (address & 0x3fffu)) % prg_size];
    }
    if (mapper == 3) return rom_prg[offset % prg_size];
    if (mapper == 4) {
        if (address < 0xa000u)
            return rom_prg[(mmc_prg_bank * 8192u + (address & 0x1fffu)) % prg_size];
        if (address >= 0xe000u)
            return rom_prg[prg_size - 8192u + (address & 0x1fffu)];
        return rom_prg[offset % prg_size];
    }
    if (mapper == 7)
        return rom_prg[(ax_bank * 32768u + offset) % prg_size];
    return prg[offset % prg_size];
}
unsigned char mapped_chr(unsigned address)
{
    if (mapper == 3)
        return rom_chr[(cn_bank * 8192u + address) % chr_size];
    if (mapper == 4 && address >= 0x1000u && address < 0x1400u)
        return rom_chr[(mmc_chr_bank * 1024u + address - 0x1000u) % chr_size];
    if (mapper == 4) return rom_chr[address % chr_size];
    return chr[address];
}
void record_log(const char *variable, unsigned addr, unsigned size, int mode)
{
    const char *path = std::getenv(variable);
    if (!path) return;
    std::FILE *out = std::fopen(path, "a");
    if (!out) return;
    std::fprintf(out, "%u %u %d\n", addr, size, mode);
    std::fclose(out);
}
}
extern "C" int nes_read(std::uint16_t address, void *buf,
                        std::uint16_t size, int mode)
{
    init();
    if (!buf) return -1;
    record_log("RETRODE_MOCK_NES_READLOG", address, size, mode);
    if (mode == NES_MODE_PRG) {
        if (address < 0x8000u ||
            static_cast<unsigned>(address) + size > 0x10000u) return -1;
        for (unsigned n = 0; n < size; ++n)
            static_cast<unsigned char *>(buf)[n] =
                mapped_prg(static_cast<unsigned>(address) + n);
        return size;
    }
    if (mode == NES_MODE_CHR) {
        if (static_cast<unsigned>(address) + size >
            (mapper == 3 || mapper == 4 ? 8192u : chr_size)) return -1;
        if ((mapper != 3 && mapper != 4) && !chr_size) return -1;
        for (unsigned n = 0; n < size; ++n)
            static_cast<unsigned char *>(buf)[n] =
                mapped_chr(static_cast<unsigned>(address) + n);
        return size;
    }
    return -1;
}
extern "C" int nes_write(std::uint16_t address, void *buf,
                         std::uint16_t size, int mode)
{
    init();
    if (!buf || mode != NES_MODE_PRG || size != 1 || address < 0x8000u)
        return -1;
    const char *fault = std::getenv("RETRODE_MOCK_NES_WRITE_FAIL");
    if (fault) return -1;
    const unsigned value = *static_cast<unsigned char *>(buf);
    // Fail on any bus-conflicting write. Sanni's unsupported fallback
    // deliberately has no equivalent in the production mapper adapter.
    if ((mapper == 2 || mapper == 3) && mapped_prg(address) != value)
        return -1;
    record_log("RETRODE_MOCK_NES_WRITELOG", address, value, mode);
    if (mapper == 2) ux_bank = value;
    else if (mapper == 3) cn_bank = value;
    else if (mapper == 4) {
        if (address == 0x8000u) mmc_mode = value & 7u;
        else if (address == 0x8001u) {
            if (mmc_mode == 6) mmc_prg_bank = value;
            if (mmc_mode == 2) mmc_chr_bank = value;
        } else return -1;
    } else if (mapper == 7) ax_bank = value & 7u;
    else return -1;
    return 1;
}
extern "C" int nes_close(void) { return 0; }
extern "C" int md_read(std::uint32_t, void *, std::uint32_t, int) { return -1; }
extern "C" int snes_read(std::uint8_t, std::uint16_t, void *, std::uint16_t, int) { return -1; }
