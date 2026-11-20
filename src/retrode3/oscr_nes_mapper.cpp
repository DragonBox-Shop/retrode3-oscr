// NES mapper bank sequencing derived from pinned original Sanni NES.cpp,
// e5a08326dfc8fff67193f7fc5a7bc36b5282b28c, readPRG/readCHR cases
// 2 (UxROM), 3 (CNROM), 4 (MMC3), 7 (AxROM). Never edit upstream sources.
// No save/flash/CHR-RAM writes. Bank registers are CPU PRG mode only.
// Mapper 1 (MMC1) needs special NES_MODE_REG timing to prevent SRAM damage,
// which the current Retrode3 kernel cannot guarantee; it remains disabled.
#include "retrode3/oscr_nes_mapper.h"
#include "retrode3/oscr_nes_compat.h"
#include "retrode3/read_bus.h"
#include "retrode3-lib.h"
#include <cstring>
#include <stdexcept>

namespace retrode3 { namespace nesmapper {
namespace {
bool power_two(unsigned n) { return n && !(n & (n - 1)); }
void check_cancel(const retrode3_host_io_v1 *io)
{
    if (!io || !io->cancelled || io->cancelled(io->context))
        throw std::runtime_error("cancelled");
}
void prg_register(uint16_t address, uint8_t data)
{
    // Never expose an arbitrary cart-write method; restrict to CPU PRG
    // mapper-register address space. Caller already validated the mapper.
    if (address < 0x8000u)
        throw std::runtime_error("NES mapper register address rejected");
#if defined(RETRODE3_TESTING) || defined(RETRODE3_NES_MAPPER_WRITE_EXPERIMENTAL)
    if (nes_write(address, &data, 1, NES_MODE_PRG) != 1)
        throw std::runtime_error("NES mapper register write failed");
#else
    (void)data;
    throw std::runtime_error("NES bank switching not hardware-approved");
#endif
}
void safe_conflict_addresses(uint16_t begin, unsigned count,
                             uint16_t *addresses, const retrode3_host_io_v1 *io)
{
    // UxROM/CNROM register writes share the ROM bus. A value written at an
    // address whose ROM byte differs may be ANDed by bus contention. Scan
    // the FIXED bank first, and refuse any missing value (no unsafe fallback).
    if (count > 32) throw std::runtime_error("too many NES conflict banks");
    bool found[32] = {};
    unsigned remaining = count;
    ReadBus bus;
    uint8_t block[512];
    for (unsigned addr = begin; addr < 65536u && remaining; addr += 512u) {
        check_cancel(io);
        bus.nes_block(static_cast<uint16_t>(addr), block, sizeof(block),
                      NesSpace::Prg);
        for (unsigned j = 0; j < 512; ++j) {
            unsigned value = block[j];
            if (value < count && !found[value]) {
                found[value] = true;
                addresses[value] = static_cast<uint16_t>(addr + j);
                --remaining;
            }
        }
    }
    if (remaining)
        throw std::runtime_error("bus-conflict-safe address not found; no NES writes");
}
void dump_prg(unsigned len, uint16_t base)
{
    OSCR::Cores::NES::dumpBankPRG(0, len, base);
}
void dump_chr(unsigned from, unsigned len)
{
    OSCR::Cores::NES::dumpBankCHR(from, from + len);
}
}

bool geometry(unsigned mapper, unsigned submapper, unsigned prg, unsigned chr)
{
    if (submapper != 0) return false;
    switch (mapper) {
    case 2: return power_two(prg) && prg >= 64 && prg <= 256 && chr == 0;
    case 3: return (prg == 16 || prg == 32) &&
                   power_two(chr) && chr >= 8 && chr <= 32;
    case 4: return power_two(prg) && prg >= 32 && prg <= 512 &&
                   (chr == 0 || (power_two(chr) && chr >= 8 && chr <= 256));
    case 7: return power_two(prg) && prg >= 64 && prg <= 256 && chr == 0;
    default: return false;
    }
}
bool runtime_writes_enabled()
{
#if defined(RETRODE3_TESTING) || defined(RETRODE3_NES_MAPPER_WRITE_EXPERIMENTAL)
    return true;
#else
    return false;
#endif
}

void dump(unsigned mapper, unsigned submapper, unsigned prg_kib,
          unsigned chr_kib, const retrode3_host_io_v1 *io)
{
    if (!geometry(mapper, submapper, prg_kib, chr_kib) ||
        !runtime_writes_enabled())
        throw std::runtime_error("NES mapper layout not approved for register writes");
    check_cancel(io);
    if (mapper == 2) { // Sanni UxROM: 16K switchable at $8000, last 16K fixed
        const unsigned count = prg_kib / 16;
        uint16_t addresses[32] = {};
        safe_conflict_addresses(0xc000, count, addresses, io);
        for (unsigned bank = 0; bank < count; ++bank) {
            check_cancel(io);
            prg_register(addresses[bank], static_cast<uint8_t>(bank));
            dump_prg(0x4000, 0x8000);
        }
    } else if (mapper == 3) { // Sanni CNROM: fixed PRG, 8K CHR banks
        check_cancel(io);
        dump_prg(prg_kib * 1024u, 0x8000);
        const unsigned count = chr_kib / 8;
        uint16_t addresses[32] = {};
        safe_conflict_addresses(0x8000, count, addresses, io);
        for (unsigned bank = 0; bank < count; ++bank) {
            check_cancel(io);
            prg_register(addresses[bank], static_cast<uint8_t>(bank));
            dump_chr(0, 0x2000);
        }
    } else if (mapper == 4) { // Sanni MMC3: PRG R6 at $8000, CHR R2 at $1000
        // Sanni also enables SRAM via $A001, unnecessary and unsafe for
        // read-only ROM dumping. Deliberately omit that write.
        for (unsigned bank = 0; bank < prg_kib / 8; ++bank) {
            check_cancel(io);
            prg_register(0x8000, 0x06);
            prg_register(0x8001, static_cast<uint8_t>(bank));
            dump_prg(0x2000, 0x8000);
        }
        for (unsigned bank = 0; bank < chr_kib; ++bank) {
            check_cancel(io);
            prg_register(0x8000, 0x02);
            prg_register(0x8001, static_cast<uint8_t>(bank));
            dump_chr(0x1000, 0x400);
        }
    } else if (mapper == 7) { // Sanni AxROM: entire switchable 32K window
        for (unsigned bank = 0; bank < prg_kib / 32; ++bank) {
            check_cancel(io);
            prg_register(0x8000, static_cast<uint8_t>(bank));
            dump_prg(0x8000, 0x8000);
        }
    }
}
} }
