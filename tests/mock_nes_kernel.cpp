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
// MMC1 serial registers and 8-KiB CHR banks, mock-only.
unsigned mmc1_shift = 0, mmc1_bits = 0, mmc1_bank = 0;
unsigned mmc1_chr = 0, mmc1_ctrl = 0x0cu;
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
        if (mapper == 1) {
            const char *p = std::getenv("RETRODE_MOCK_NES_PRG_KIB");
            const char *c = std::getenv("RETRODE_MOCK_NES_CHR_KIB");
            if (p) prg_size = static_cast<unsigned>(std::atoi(p)) * 1024u;
            if (c) chr_size = static_cast<unsigned>(std::atoi(c)) * 1024u;
        }
        rom_prg.resize(prg_size);
        rom_chr.resize(chr_size);
        for (unsigned i = 0; i < prg_size; ++i) rom_prg[i] = pattern_prg(i);
        for (unsigned i = 0; i < chr_size; ++i) rom_chr[i] = pattern_chr(i);
        if (mapper == 1 && prg_size >= 32768u &&
            std::getenv("RETRODE_MOCK_NES_DRMARIO_PROBE")) {
            // 508 zero bytes + CRC32 patch = 0x6607DBDA, the pinned
            // Dr. Mario Europe MMC1 (submapper 5) first-bank fingerprint.
            // Emulates a DB match without bundling a copyrighted ROM.
            std::memset(&rom_prg[0], 0, 508);
            rom_prg[508] = 0x80;
            rom_prg[509] = 0x73;
            rom_prg[510] = 0x12;
            rom_prg[511] = 0x9e;
            if (std::getenv("RETRODE_MOCK_NES_DRMARIO_CRC") &&
                prg_size == 32768u && chr_size == 32768u) {
                // Synthetic ROM payload matched to the pinned complete
                // Dr. Mario Europe CRC32 9735D267 using four changed
                // CHR bytes. This is NOT the Nintendo ROM content.
                rom_chr[chr_size - 4] = 0x03;
                rom_chr[chr_size - 3] = 0x03;
                rom_chr[chr_size - 2] = 0x71;
                rom_chr[chr_size - 1] = 0x00;
            }
        }
        if (mapper == 1 && prg_size >= 32768u &&
            std::getenv("RETRODE_MOCK_NES_BATTERY_PROBE")) {
            // The actual Sanni A Ressha de Ikou first-window CRC32
            // A24E2D45, synthesized entirely from zeroes + four bytes.
            std::memset(&rom_prg[0], 0, 508);
            rom_prg[508] = 0x09;
            rom_prg[509] = 0x85;
            rom_prg[510] = 0x01;
            rom_prg[511] = 0x8d;
        }
        if (mapper == 1 && prg_size == 128u * 1024u &&
            chr_size == 0u && std::getenv("RETRODE_MOCK_NES_RAD_RACER_REF")) {
            // A synthetic first 512 bytes with Sanni's Highway Star ID32A.
            // The PAL Rad Racer reference has the same read layout but
            // a DIFFERENT complete PRG CRC. No game data is included.
            std::memset(&rom_prg[0], 0, 508);
            rom_prg[508] = 0xb4; rom_prg[509] = 0x26;
            rom_prg[510] = 0x43; rom_prg[511] = 0x91;
            rom_prg[prg_size - 4] = 0x5f;
            rom_prg[prg_size - 3] = 0x0a;
            rom_prg[prg_size - 2] = 0x3c;
            rom_prg[prg_size - 1] = 0x07;
        }
        if (mapper == 1 && prg_size == 128u * 1024u &&
            chr_size == 32u * 1024u &&
            std::getenv("RETRODE_MOCK_NES_MARIO_YOSHI_EU")) {
            // Correct full Europe ROM CRC, but intentionally *no* Sanni
            // fast-probe fingerprint: the PAL title is missing there.
            rom_chr[chr_size - 4] = 0x92;
            rom_chr[chr_size - 3] = 0xe1;
            rom_chr[chr_size - 2] = 0xfe;
            rom_chr[chr_size - 1] = 0x6c;
        }
        if (mapper == 1 && prg_size == 256u * 1024u &&
            chr_size == 0u && std::getenv("RETRODE_MOCK_NES_FAXANADU_EU")) {
            // Original Sanni entry exists, but deliberately no matching
            // fingerprint in the mock. Full ROM CRC should still verify.
            rom_prg[prg_size - 4] = 0x4a;
            rom_prg[prg_size - 3] = 0x0f;
            rom_prg[prg_size - 2] = 0xd2;
            rom_prg[prg_size - 1] = 0x99;
        }
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
    if (mapper == 1) {
        unsigned bank = address < 0xc000u ? mmc1_bank : prg_size / 16384u - 1u;
        if (prg_size == 512u * 1024u) {
            // SUROM high half selected by CHR register bit 4.
            bank = (bank & 15u) | ((mmc1_chr & 0x10u) ? 16u : 0u);
        }
        return rom_prg[(bank * 16384u + (address & 0x3fffu)) % prg_size];
    }
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
    if (mapper == 1)
        return rom_chr[((mmc1_chr & 0x1eu) * 4096u + address) % chr_size];
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
    if (mapper == 1) {
        if (value & 0x80u) {
            mmc1_shift = mmc1_bits = 0;
        } else {
            mmc1_shift |= (value & 1u) << mmc1_bits;
            if (++mmc1_bits == 5u) {
                if (address >= 0xe000u) mmc1_bank = mmc1_shift & 15u;
                else if (address >= 0xa000u && address < 0xc000u)
                    mmc1_chr = mmc1_shift & 31u;
                else if (address == 0x8000u)
                    mmc1_ctrl = mmc1_shift & 31u;
                else return -1;
                mmc1_shift = mmc1_bits = 0;
            }
        }
    }
    else if (mapper == 2) ux_bank = value;
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
