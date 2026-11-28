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
unsigned family_chr[8] = {};
bool family_mmc3(unsigned m)
{
    return m == 64 || m == 76 || m == 88 || m == 95 || m == 118 ||
           m == 119 || m == 154 || m == 158;
}
bool family_direct(unsigned m)
{
    switch (m) {
    case 18: case 22: case 24: case 26: case 32: case 33: case 48:
    case 65: case 68: case 112: case 117:
    case 21: case 25: case 85: case 210: return true;
    default: return false;
    }
}
bool family_latch(unsigned m)
{
    return m == 70 || m == 89 || m == 152 || m == 180 || m == 241 ||
           m == 30 || m == 78 || m == 94;
}
bool family_new(unsigned m)
{
    return family_mmc3(m) || m == 9 || m == 10 || m == 69 ||
           family_direct(m) || family_latch(m) || m == 28;
}
unsigned direct_prg[2] = {}, direct_chr[8] = {};
unsigned action53_select = 0xffu;
unsigned smd_window = 0x6000u, smd_outer[4] = {};
bool smd_wram_enabled = true; // require explicit disabling before outer writes
unsigned smd_seen = 0;
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
        if (mapper == 11 || mapper == 66) { prg_size = 131072u; chr_size = 32768u; }
        if (mapper == 13) { prg_size = 32768u; chr_size = 0u; }
        if (mapper == 71) { prg_size = 131072u; chr_size = 0u; }
        if (mapper == 206) { prg_size = 131072u; chr_size = 65536u; }
        if (mapper == 268) {
            prg_size = 512u * 1024u; chr_size = 0u;
            const char *sub = std::getenv("RETRODE_MOCK_NES_SUBMAPPER");
            if (sub && std::atoi(sub) == 1) smd_window = 0x5000u;
        }
        if (mapper == 1 || mapper == 11 || mapper == 13 || mapper == 66 || mapper == 71 || mapper == 206 || family_new(mapper)) {
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
            // Synthetic ROM with the EXACT five-times-stable hardware
            // probe pair measured on the PAL NES-SFROM cartridge:
            // 512-byte CRCs at CPU $8000 = 29D1D575, $E000 = 9A78A49D.
            // Final 4 CHR bytes preserve its full payload CRC 836685C4.
            // The reference PRG/CHR game data is NOT included.
            std::memset(&rom_prg[0], 0, 508);
            rom_prg[508] = 0x17; rom_prg[509] = 0xf0;
            rom_prg[510] = 0x59; rom_prg[511] = 0xd8;
            std::memset(&rom_prg[prg_size - 8192u], 0, 508);
            rom_prg[prg_size - 8192u + 508] = 0x2a;
            rom_prg[prg_size - 8192u + 509] = 0xda;
            rom_prg[prg_size - 8192u + 510] = 0x07;
            rom_prg[prg_size - 8192u + 511] = 0x38;
            rom_chr[chr_size - 4] = 0x22;
            rom_chr[chr_size - 3] = 0x63;
            rom_chr[chr_size - 2] = 0x37;
            rom_chr[chr_size - 1] = 0x4f;
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
        if (mapper == 268 && std::getenv("RETRODE_MOCK_NES_METAL_STORM")) {
            // Synthetic 512 KiB PRG only. Four-byte CRC patches reproduce
            // measured probe and independent reference CRC, not game data.
            std::memset(&rom_prg[0], 0, 512);
            rom_prg[508] = 0x47;
            rom_prg[509] = 0x81;
            rom_prg[510] = 0x49;
            rom_prg[511] = 0xca;
            std::memset(&rom_prg[prg_size - 8192u], 0, 512);
            if (!std::getenv("RETRODE_MOCK_NES_METAL_STORM_BAD_CRC")) {
                rom_prg[prg_size - 4u] = 0x2f;
                rom_prg[prg_size - 3u] = 0xef;
                rom_prg[prg_size - 2u] = 0xc9;
                rom_prg[prg_size - 1u] = 0x9b;
            }
            if (std::getenv("RETRODE_MOCK_NES_METAL_STORM_BAD_PROBE"))
                rom_prg[prg_size - 8192u] = 1u;
        }
        // Emulate Metal Storm's uninformative $E000 probe window.
        // The rest is synthetic, not a copy of a commercial ROM.
        if (mapper == 4 && std::getenv("RETRODE_MOCK_NES_CONSTANT_LAST"))
            std::memset(&rom_prg[prg_size - 8192u], 0, 512);
        const char *no_safe = std::getenv("RETRODE_MOCK_NES_NO_CONFLICT_SAFE");
        if (no_safe && mapper == 2)
            for (unsigned i = prg_size - 16384u; i < prg_size; ++i)
                rom_prg[i] = 0xff;
        if (no_safe && (mapper == 3 || mapper == 7)) {
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
    if (mapper == 2 || mapper == 71) {
        unsigned bank = address < 0xc000u ? ux_bank : prg_size / 16384u - 1u;
        return rom_prg[(bank * 16384u + (address & 0x3fffu)) % prg_size];
    }
    if (mapper == 3 || mapper == 13 || (mapper == 206 && prg_size == 32768u)) return rom_prg[offset % prg_size];
    if (mapper == 268) {
        // Test the generated outer-bank sequence independently of inner
        // banks; this simplified 512 KiB fixture is not a full ASIC model.
        if (address < 0xa000u) {
            const unsigned bank = (smd_outer[0] & 3u) * 16u + (mmc_prg_bank & 15u);
            return rom_prg[bank * 8192u + (address & 0x1fffu)];
        }
        if (address >= 0xe000u)
            return rom_prg[prg_size - 8192u + (address & 0x1fffu)];
        return rom_prg[offset % prg_size];
    }
    if (mapper == 28)
        return rom_prg[(ax_bank * 32768u + offset) % prg_size];
    if (family_latch(mapper)) {
        if (mapper == 241)
            return rom_prg[(ax_bank * 32768u + offset) % prg_size];
        unsigned bank;
        if (mapper == 180)
            bank = address < 0xc000u ? 0u : ux_bank;
        else
            bank = address < 0xc000u ? ux_bank : prg_size / 16384u - 1u;
        return rom_prg[(bank * 16384u + (address & 0x3fffu)) % prg_size];
    }
    if (family_direct(mapper)) {
        if (mapper == 24 || mapper == 26 || mapper == 68) {
            unsigned bank = address < 0xc000u ? direct_prg[0] : prg_size / 16384u - 1u;
            return rom_prg[(bank * 16384u + (address & 0x3fffu)) % prg_size];
        }
        if (address < 0xc000u) {
            unsigned window = (address - 0x8000u) >> 13;
            return rom_prg[(direct_prg[window] * 8192u + (address & 0x1fffu)) % prg_size];
        }
        return rom_prg[(prg_size - 16384u + (address & 0x3fffu)) % prg_size];
    }
    if (mapper == 10) {
        if (address < 0xc000u)
            return rom_prg[(mmc_prg_bank * 16384u + (address & 0x3fffu)) % prg_size];
        return rom_prg[prg_size - 16384u + (address & 0x3fffu)];
    }
    if (mapper == 4 || mapper == 206 || family_new(mapper)) {
        if (address < 0xa000u)
            return rom_prg[(mmc_prg_bank * 8192u + (address & 0x1fffu)) % prg_size];
        if (address >= 0xe000u)
            return rom_prg[prg_size - 8192u + (address & 0x1fffu)];
        return rom_prg[offset % prg_size];
    }
    if (mapper == 7 || mapper == 11 || mapper == 66)
        return rom_prg[(ax_bank * 32768u + offset) % prg_size];
    return prg[offset % prg_size];
}
unsigned char mapped_chr(unsigned address)
{
    if (mapper == 1)
        return rom_chr[((mmc1_chr & 0x1eu) * 4096u + address) % chr_size];
    if (mapper == 3 || mapper == 11 || mapper == 66)
        return rom_chr[(cn_bank * 8192u + address) % chr_size];
    if (family_latch(mapper))
        return rom_chr[(cn_bank * 8192u + address) % chr_size];
    if (family_direct(mapper)) {
        if (mapper == 85)
            return rom_chr[(direct_chr[address >> 10] * 1024u + (address & 0x3ffu)) % chr_size];
        if (mapper == 33 || mapper == 48)
            return rom_chr[(direct_chr[0] * 2048u + (address & 0x7ffu)) % chr_size];
        if (mapper == 68) {
            unsigned window = address >> 11;
            return rom_chr[(direct_chr[window] * 2048u + (address & 0x7ffu)) % chr_size];
        }
        return rom_chr[(direct_chr[0] * 1024u + (address & 0x3ffu)) % chr_size];
    }
    if (mapper == 9 || mapper == 10)
        return rom_chr[(family_chr[0] * 4096u + (address & 0xfffu)) % chr_size];
    if (mapper == 69)
        return rom_chr[(family_chr[0] * 1024u + (address & 0x3ffu)) % chr_size];
    if (mapper == 76)
        return rom_chr[(family_chr[2] * 2048u + (address & 0x7ffu)) % chr_size];
    if (mapper == 88 || mapper == 154) {
        if (address < 0x800u)
            return rom_chr[((family_chr[0] & ~1u) * 1024u + address) % chr_size];
        if (address >= 0x1000u && address < 0x1800u) {
            unsigned reg = 2u + ((address - 0x1000u) >> 10);
            return rom_chr[((64u | (family_chr[reg] & 63u)) * 1024u +
                            (address & 0x3ffu)) % chr_size];
        }
    }
    if (family_mmc3(mapper) && address >= 0x1000u && address < 0x1400u)
        return rom_chr[(family_chr[2] * 1024u + (address & 0x3ffu)) % chr_size];
    if (family_mmc3(mapper)) return rom_chr[address % chr_size];
    if ((mapper == 4 || mapper == 206) && address >= 0x1000u && address < 0x1400u)
        return rom_chr[(mmc_chr_bank * 1024u + address - 0x1000u) % chr_size];
    if (mapper == 4 || mapper == 206) return rom_chr[address % chr_size];
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
    if (!buf || mode != NES_MODE_PRG || size != 1 ||
        (address < 0x8000u && mapper != 268 && !(mapper == 28 && address == 0x5000u)))
        return -1;
    const char *fault = std::getenv("RETRODE_MOCK_NES_WRITE_FAIL");
    if (fault) return -1;
    const unsigned value = *static_cast<unsigned char *>(buf);
    if (mapper == 268) {
        const bool no_prg_ram = std::getenv("RETRODE_MOCK_NES_METAL_STORM") != NULL;
        if (address == 0xa001u && (value == 0u || (no_prg_ram && value == 0x80u))) {
            smd_wram_enabled = value == 0x80u;
        } else if (address >= smd_window && address < smd_window + 4u) {
            if (smd_wram_enabled && !no_prg_ram) return -1;
            const unsigned reg = address - smd_window;
            if ((reg == 0u && value > 3u) ||
                (reg == 1u && value != 0x60u) ||
                (reg >= 2u && value != 0u)) return -1;
            smd_outer[reg] = value;
            smd_seen |= 1u << reg;
        } else if (no_prg_ram && address >= 0x5000u && address < 0x5004u) {
            // The other decode window is unconnected on the submapper-0
            // no-PRG-RAM fixture. Record it, but do not change its banks.
        } else if (address == 0x8000u && value == 6u) {
            if ((smd_wram_enabled && !no_prg_ram) || smd_seen != 15u) return -1;
            mmc_mode = 6u;
        } else if (address == 0x8001u && value < 64u) {
            if ((smd_wram_enabled && !no_prg_ram) || mmc_mode != 6u || smd_seen != 15u ||
                smd_outer[0] != (value >> 4)) return -1;
            mmc_prg_bank = value;
            smd_seen = 0;
        } else return -1; // wrong window, WRAM enable, or other write
        record_log("RETRODE_MOCK_NES_WRITELOG", address, value, mode);
        return 1;
    }
    if (mapper == 28) {
        if (address == 0x5000u &&
            (value == 0u || value == 1u || value == 0x80u || value == 0x81u))
            action53_select = value;
        else if (address == 0x8000u && action53_select == 0x81u && value < 32u)
            ax_bank = value;
        else if (!(address == 0x8000u && action53_select != 0xffu &&
                   action53_select != 0x81u && value == 0u))
            return -1;
        record_log("RETRODE_MOCK_NES_WRITELOG", address, value, mode);
        return 1;
    }
    // Fail on any bus-conflicting write. Sanni's unsupported fallback
    // deliberately has no equivalent in the production mapper adapter.
    if ((mapper == 2 || mapper == 3 || mapper == 7 || mapper == 11 || mapper == 66 || mapper == 71 || family_latch(mapper)) && mapped_prg(address) != value)
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
    else if (mapper == 2 || mapper == 71) ux_bank = value;
    else if (mapper == 3) cn_bank = value;
    else if (family_latch(mapper)) {
        if (mapper == 30) {
            if (address < 0xc000u || value >= 32u) return -1;
            ux_bank = value;
        } else if (mapper == 94) {
            if (address < 0xc000u || value > 28u || (value & 3u)) return -1;
            ux_bank = value >> 2;
        } else if (mapper == 78) {
            ux_bank = value & 7u;
            cn_bank = value >> 4;
        } else if (mapper == 180) ux_bank = value & 7u;
        else if (mapper == 241) ax_bank = value & 15u;
        else {
            ux_bank = (value >> 4) & 7u;
            cn_bank = mapper == 89 ? ((value & 7u) | ((value & 0x80u) >> 4)) : value & 15u;
        }
    }
    else if (family_direct(mapper)) {
        switch (mapper) {
        case 18:
            if (address >= 0x8000u && address <= 0x8003u && value < 16u) {
                unsigned reg = (address - 0x8000u) >> 1;
                if (address & 1u) direct_prg[reg] = (direct_prg[reg] & 15u) | (value << 4);
                else direct_prg[reg] = (direct_prg[reg] & 240u) | value;
            } else if (address == 0xa000u && value < 16u)
                direct_chr[0] = (direct_chr[0] & 240u) | value;
            else if (address == 0xa001u && value < 16u)
                direct_chr[0] = (direct_chr[0] & 15u) | (value << 4);
            else return -1;
            break;
        case 21:
            if (address == 0xa000u && value < 32u) direct_prg[1] = value;
            else if (address == 0xb000u && value < 16u)
                direct_chr[0] = (direct_chr[0] & 240u) | value;
            else if (address == 0xb002u && value < 8u)
                direct_chr[0] = (direct_chr[0] & 15u) | (value << 4);
            else return -1;
            break;
        case 25:
            if (address == 0x9005u && value == 0u) {}
            else if (address == 0x8000u && value < 32u) direct_prg[0] = value;
            else if (address == 0xb000u && value < 16u)
                direct_chr[0] = (direct_chr[0] & 240u) | value;
            else if (address == 0xb00au && value < 16u)
                direct_chr[0] = (direct_chr[0] & 15u) | (value << 4);
            else return -1;
            break;
        case 85:
            if (address == 0x8000u && value < 32u) direct_prg[0] = value;
            else if (address >= 0xa000u && address <= 0xd008u &&
                     ((address & 0xfffu) == 0u || (address & 0xfffu) == 8u))
                direct_chr[((address - 0xa000u) >> 12) * 2u + ((address & 8u) >> 3)] = value;
            else return -1;
            break;
        case 210:
            if (address == 0xe000u && value < 32u) direct_prg[0] = value;
            else if (address == 0xe800u && value < 32u) direct_prg[1] = value;
            else if (address == 0xe800u && value == 0xc0u) direct_prg[1] = 0u;
            else if (address == 0x8000u) direct_chr[0] = value;
            else return -1;
            break;
        case 22:
            if (address == 0x8000u && value < 16u) direct_prg[0] = value;
            else if (address == 0xb000u && value < 16u)
                direct_chr[0] = (direct_chr[0] & 0x78u) | (value >> 1);
            else if (address == 0xb002u && value < 16u)
                direct_chr[0] = (direct_chr[0] & 7u) | (value << 3);
            else return -1;
            break;
        case 24: case 26:
            if (address == 0x8000u && value < 16u) direct_prg[0] = value;
            else if (address == 0xb003u && value == 0u) {}
            else if (address == 0xd000u) direct_chr[0] = value;
            else return -1;
            break;
        case 32:
            if (address == 0x9000u && value == 1u) {}
            else if (address == 0xa000u && value < 32u) direct_prg[1] = value;
            else if (address == 0xb000u) direct_chr[0] = value;
            else return -1;
            break;
        case 33: case 48:
            if (address == 0x8000u && value < 32u) direct_prg[0] = value;
            else if (address == 0x8001u && value < 32u) direct_prg[1] = value;
            else if (address == 0x8002u) direct_chr[0] = value;
            else return -1;
            break;
        case 65: case 117:
            if (address == 0x8000u && value < 32u) direct_prg[0] = value;
            else if (address == (mapper == 65 ? 0xb000u : 0xa000u)) direct_chr[0] = value;
            else return -1;
            break;
        case 68:
            if (address == 0xf000u && value < 8u) direct_prg[0] = value;
            else if (address >= 0x8000u && address <= 0xb000u && !(address & 0xfffu))
                direct_chr[(address - 0x8000u) >> 12] = value;
            else return -1;
            break;
        case 112:
            if (address == 0x8000u && (value == 0u || value == 6u)) mmc_mode = value;
            else if (address == 0xa000u && mmc_mode == 0u && value < 32u)
                direct_prg[0] = value;
            else if (address == 0xa000u && mmc_mode == 6u) direct_chr[0] = value;
            else return -1;
            break;
        default: return -1;
        }
    }
    else if (mapper == 9 || mapper == 10) {
        if (address == 0xa000u && value < 16u) mmc_prg_bank = value;
        else if (address == 0xb000u && value < 32u) family_chr[0] = value;
        else if (address == 0xc000u && value < 32u) family_chr[1] = value;
        else return -1;
    } else if (mapper == 69) {
        if (address == 0x8000u && (value == 0 || value == 8 || value == 9))
            mmc_mode = value;
        else if (address == 0xa000u) {
            if (mmc_mode == 0) family_chr[0] = value;
            else if (mmc_mode == 9 && value < 32u) mmc_prg_bank = value;
            else if (mmc_mode != 8 || value != 0) return -1;
        } else return -1;
    } else if (family_mmc3(mapper)) {
        if (address == 0x8000u && (value == 0 || value == 2 || value == 3 || value == 6))
            mmc_mode = value;
        else if (address == 0x8001u) {
            if (mmc_mode == 6) mmc_prg_bank = value;
            else if (mmc_mode == 0 || mmc_mode == 2 || mmc_mode == 3) {
                if (mapper == 119 && value >= 64u) return -1;
                family_chr[mmc_mode] = value;
            } else return -1;
        } else return -1;
    }
    else if (mapper == 4 || mapper == 206) {
        if (address == 0x8000u) mmc_mode = value & 7u;
        else if (address == 0x8001u) {
            if (mmc_mode == 6) mmc_prg_bank = value;
            if (mmc_mode == 2) mmc_chr_bank = value;
        } else return -1;
    } else if (mapper == 7) ax_bank = value & 7u;
    else if (mapper == 11) { ax_bank = value & 3u; cn_bank = value >> 4; }
    else if (mapper == 66) { ax_bank = (value >> 4) & 3u; cn_bank = value & 3u; }
    else return -1;
    return 1;
}
extern "C" int nes_close(void) { return 0; }
extern "C" int md_read(std::uint32_t, void *, std::uint32_t, int) { return -1; }
extern "C" int snes_read(std::uint8_t, std::uint16_t, void *, std::uint16_t, int) { return -1; }
