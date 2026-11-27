// Runtime approval and HAL for automatically generated Sanni NES switches.
// No local ROM banking loops. Unsupported hardware primitives fail closed.
#include "retrode3/oscr_nes_mapper.h"
#include "retrode3/oscr_nes_compat.h"
#include "retrode3/read_bus.h"
#include "retrode3-lib.h"
#include <cstring>
#include <cstdlib>
#include <stdexcept>

namespace retrode3 { namespace nesmapper {
namespace {
bool power_two(unsigned n) { return n && !(n & (n - 1)); }
void check_cancel(const retrode3_host_io_v1 *io)
{
    if (!io || !io->cancelled || io->cancelled(io->context))
        throw std::runtime_error("cancelled");
}
// The kernel's NES_MODE_PRG implements a normal CPU write cycle and
// derives ROMSEL from A15. Below $8000 this is NOT a PRG-ROM access.
// Keep the address spaces distinct; only reviewed mapper policies below
// may authorize a low CPU address. Never fall back to NES_MODE_RAM/WRAM.
enum class CpuRegisterSpace { Prg, Expansion, WramOverlay };
void cpu_mapper_register(CpuRegisterSpace space, uint16_t address, uint8_t data)
{
    const bool valid = space == CpuRegisterSpace::Prg ? address >= 0x8000u :
        space == CpuRegisterSpace::Expansion ? address >= 0x4020u && address < 0x6000u :
        address >= 0x6000u && address < 0x8000u;
    if (!valid)
        throw std::runtime_error("NES mapper register address rejected");
    if (nes_write(address, &data, 1, NES_MODE_PRG) != 1)
        throw std::runtime_error("NES mapper register write failed");
}
void prg_register(uint16_t address, uint8_t data)
{
    cpu_mapper_register(CpuRegisterSpace::Prg, address, data);
}
void mmc1_register(uint16_t address, uint8_t data)
{
    // MMC1 serial register writes are required by *ordinary ROM reads*,
    // including CHR-ROM. Keep this separate from the experimental gates
    // for other NES mappers; never permit writes below $8000.
    if (address != 0x8000u && address != 0xa000u &&
        address != 0xe000u)
        throw std::runtime_error("invalid MMC1 register address");
    if (nes_write(address, &data, 1, NES_MODE_PRG) != 1)
        throw std::runtime_error("MMC1 serial register write failed");
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
uint16_t safe_register_address(uint16_t begin, uint8_t value, const retrode3_host_io_v1 *io)
{
    // Reusable conflict-safe register primitive for boards with ROM on
    // the write bus. Search only the board's decoded register window,
    // using the CURRENT bank, never the bank about to be selected.
    ReadBus bus;
    uint8_t block[512];
    for (unsigned addr = begin; addr < 65536u; addr += sizeof(block)) {
        check_cancel(io);
        bus.nes_block(static_cast<uint16_t>(addr), block, sizeof(block),
                      NesSpace::Prg);
        for (unsigned j = 0; j < sizeof(block); ++j)
            if (block[j] == value)
                return static_cast<uint16_t>(addr + j);
    }
    throw std::runtime_error("NES bus-conflict-safe register address not found");
}

// The active context is scoped to one CLI operation (the CLI owns the bus lock).
struct GeneratedContext {
    unsigned mapper;
    unsigned submapper;
    bool wram_disabled;
    const retrode3_host_io_v1 *io;
    unsigned cache_base;
    uint8_t cache[512];
    bool cache_valid;
};
GeneratedContext *active = NULL;
struct ContextScope {
    explicit ContextScope(GeneratedContext &c) {
        if (active) throw std::runtime_error("nested NES generated read");
        active = &c;
    }
    ~ContextScope() { active = NULL; }
};
GeneratedContext &context()
{
    if (!active) throw std::runtime_error("no NES generated read context");
    check_cancel(active->io);
    return *active;
}
unsigned exponent(unsigned kib, unsigned unit)
{
    unsigned n = 0;
    for (unsigned v = kib / unit; v > 1; v >>= 1) ++n;
    return n;
}
void unsupported()
{
    throw std::runtime_error("NES upstream hardware primitive not approved");
}
}
bool geometry(unsigned mapper, unsigned submapper, unsigned prg, unsigned chr)
{
    // SMD133: deliberately start with the 512 KiB PRG / 256 KiB
    // CHR-RAM profile used by the Retro-Bit rerelease. No CHR-ROM reads,
    // no later ASIC variants or arbitrary multicart geometries.
    if (mapper == 268)
        return submapper <= 1u && prg == 512u && chr == 0u;
    if (mapper == 1)
        return (submapper == 0 || (submapper == 5 && prg == 32u)) &&
               power_two(prg) && prg >= 32 && prg <= 512 &&
               (chr == 0 || (power_two(chr) && chr >= 8 && chr <= 128));
    // NES 2.0: 0 unspecified, 1 no bus conflicts, 2 AND conflicts
    // for discrete UxROM/CNROM/AxROM. Keep conflict-safe selection for
    // all three because a probe fingerprint alone is not board proof.
    if (mapper == 2 || mapper == 3 || mapper == 7) {
        if (submapper > 2) return false;
    } else if (submapper != 0) return false;
    switch (mapper) {
    case 2: return power_two(prg) && prg >= 64 && prg <= 256 && chr == 0;
    case 3: return (prg == 16 || prg == 32) &&
                   power_two(chr) && chr >= 8 && chr <= 32;
    case 4: return power_two(prg) && prg >= 32 && prg <= 512 &&
                   (chr == 0 || (power_two(chr) && chr >= 8 && chr <= 256));
    case 7: return power_two(prg) && prg >= 64 && prg <= 256 && chr == 0;
    case 11: return power_two(prg) && prg >= 32 && prg <= 128 &&
                    (chr == 0 || (power_two(chr) && chr >= 8 && chr <= 128));
    case 13: return prg == 32 && chr == 0; // CPROM: ROM read only, no CHR-RAM writes.
    case 66: return power_two(prg) && prg >= 32 && prg <= 128 &&
                    power_two(chr) && chr >= 8 && chr <= 32;
    case 71: return power_two(prg) && prg >= 64 && prg <= 256 && chr == 0;
    case 206: return power_two(prg) && prg >= 32 && prg <= 128 &&
                     power_two(chr) && chr >= 16 && chr <= 64;
    default: return false;
    }
}
bool mmc1_lab_enabled()
{
    // Kept for source ABI compatibility; mapper 1 is normal ROM-read support.
    return true;
}
bool mmc1_layout_enabled(unsigned submapper, unsigned prg,
                         unsigned chr, bool battery)
{
    // Battery-backed MMC1 boards require an SRAM-safe M2/ROMSEL write
    // pulse not yet provided by the kernel. No manual override bypass.
    return !battery && geometry(1u, submapper, prg, chr);
}
bool runtime_writes_enabled()
{
    return true;
}

bool runtime_mapper_enabled(unsigned mapper)
{
    // Explicit list: newly ported geometry alone must not enable writes.
    return mapper == 1 || mapper == 2 || mapper == 3 ||
           mapper == 4 || mapper == 7 || mapper == 11 || mapper == 13 ||
           mapper == 66 || mapper == 71 || mapper == 206 || mapper == 268;
}

void dump(unsigned mapper, unsigned submapper, unsigned prg_kib,
          unsigned chr_kib, const retrode3_host_io_v1 *io)
{
    if (!geometry(mapper, submapper, prg_kib, chr_kib) ||
        !(mapper == 1 ? mmc1_layout_enabled(submapper, prg_kib, chr_kib, false)
                       : runtime_mapper_enabled(mapper)))
        throw std::runtime_error("NES mapper layout not approved for register writes");
    check_cancel(io);
    // Preflight all fixed-bank conflict values before the first write.
    uint16_t addresses[32] = {};
    if (mapper == 2) safe_conflict_addresses(0xc000, prg_kib / 16, addresses, io);
    if (mapper == 3) safe_conflict_addresses(0x8000, chr_kib / 8, addresses, io);
    GeneratedContext state = {mapper, submapper, false, io, 0, {}, false};
    ContextScope scope(state);
    OSCR::Cores::NES::generatedReadPRG(mapper, exponent(prg_kib, 16));
    if (chr_kib)
        OSCR::Cores::NES::generatedReadCHR(mapper, exponent(chr_kib, 4));
}

void generated_write(unsigned address, unsigned data, bool serial)
{
    GeneratedContext &c = context();
    if (address > 0xffffu) unsupported();
    if (c.mapper == 1) {
        // Original MMC1 serial helper passes the shifted byte; the NES
        // serial input consumes bit 0. Keep the existing no-SRAM pulse mode.
        if (serial && address != 0xe000u) unsupported();
        if (!serial && address == 0xe000u) unsupported();
        mmc1_register(static_cast<uint16_t>(address),
                      static_cast<uint8_t>(data == 0x80u && address == 0x8000u ?
                                           0x80u : data & 1u));
    } else {
        if (serial || data > 255u) unsupported();
        if (c.mapper == 268) {
            // Upstream emits both address windows for Coolboy/Mindkids.
            // Only the explicitly selected submapper's window is driven.
            // For this 512 KiB ROM-only profile the outer values are
            // tightly bounded; no SRAM enable, lockout, or flash commands.
            if (address == 0xa001u && data == 0x80u) {
                // Upstream enables MMC3 WRAM before selecting the outer
                // registers. They overlay but are independent of WRAM.
                // Keep it disabled so $6000 writes cannot alter save RAM.
                prg_register(0xa001u, 0u);
                c.wram_disabled = true;
            } else if ((address >= 0x5000u && address <= 0x5003u) ||
                       (address >= 0x6000u && address <= 0x6003u)) {
                if (!c.wram_disabled) unsupported();
                const unsigned reg = address & 3u;
                if ((reg == 0u && data > 3u) ||
                    (reg == 1u && data != 0x60u) ||
                    (reg >= 2u && data != 0u)) unsupported();
                const unsigned window = c.submapper == 0u ? 0x6000u : 0x5000u;
                if ((address & 0xf000u) == window)
                    cpu_mapper_register(c.submapper == 0u ? CpuRegisterSpace::WramOverlay :
                        CpuRegisterSpace::Expansion, static_cast<uint16_t>(address),
                        static_cast<uint8_t>(data));
            } else {
                if (!c.wram_disabled ||
                    !((address == 0x8000u && data == 6u) ||
                      (address == 0x8001u && data < 64u))) unsupported();
                prg_register(static_cast<uint16_t>(address), static_cast<uint8_t>(data));
            }
            c.cache_valid = false;
            return;
        }
        if (c.mapper == 4 || c.mapper == 206) {
            // MMC3/DxROM register family: never enable writable SRAM.
            if (address == 0xa001u && data == 0x80u) return;
            if (address != 0x8000u && address != 0x8001u) unsupported();
        } else if (c.mapper == 7) {
            if (address != 0x8000u) unsupported();
            address = safe_register_address(0x8000u, static_cast<uint8_t>(data), c.io);
        } else if (c.mapper == 11 || c.mapper == 66 || c.mapper == 71) {
            // Discrete latch boards: the generated upstream code owns all
            // bank values. Restrict its register decode and make each write
            // electrically conflict-safe, even after a PRG bank switch.
            if (c.mapper == 11 && (address < 0xffb0u || address > 0xffbfu || (data & ~0xf3u))) unsupported();
            if (c.mapper == 66 && (address < 0x8000u || (data & ~0x33u))) unsupported();
            if (c.mapper == 71 && (address != 0xc000u || data > 15u)) unsupported();
            address = safe_register_address(c.mapper == 71 ? 0xc000u : 0x8000u,
                                            static_cast<uint8_t>(data), c.io);
        } else if (c.mapper == 2 || c.mapper == 3) {
            if (address < (c.mapper == 2 ? 0xc000u : 0x8000u) ||
                ReadBus().nes_byte(static_cast<uint16_t>(address), NesSpace::Prg) != data)
                throw std::runtime_error("bus-conflict-safe address not found");
        } else unsupported();
        prg_register(static_cast<uint16_t>(address), static_cast<uint8_t>(data));
    }
    c.cache_valid = false;
}

uint8_t generated_read(unsigned address)
{
    GeneratedContext &c = context();
    if ((c.mapper != 2 && c.mapper != 3 && c.mapper != 66) || address > 0xffffu ||
        address < (c.mapper == 2 ? 0xc000u : 0x8000u)) unsupported();
    unsigned base = address & ~511u;
    if (!c.cache_valid || c.cache_base != base) {
        ReadBus().nes_block(static_cast<uint16_t>(base), c.cache, sizeof(c.cache), NesSpace::Prg);
        c.cache_base = base;
        c.cache_valid = true;
    }
    return c.cache[address - base];
}
void generated_unsupported() { unsupported(); }
} }

namespace OSCR {
namespace UI {
void printLineSync(const char *) {
    retrode3::nesmapper::generated_unsupported();
}
}
namespace Cores { namespace NES {
void write_prg_byte(unsigned a, unsigned v) { retrode3::nesmapper::generated_write(a,v,false); }
void write_reg_byte(unsigned a, unsigned v) { retrode3::nesmapper::generated_write(a,v,true); }
std::uint8_t read_prg_byte(unsigned a) { return retrode3::nesmapper::generated_read(a); }
std::uint8_t read_chr_byte(unsigned) { retrode3::nesmapper::generated_unsupported(); return 0; }
void write_ram_byte(unsigned, unsigned) { retrode3::nesmapper::generated_unsupported(); }
void write_prg_pulsem2(unsigned, unsigned) { retrode3::nesmapper::generated_unsupported(); }
void write_reg_m59(unsigned) { retrode3::nesmapper::generated_unsupported(); }
void dumpPRG_pulsem2(unsigned, unsigned) { retrode3::nesmapper::generated_unsupported(); }
void dumpCHR_pulsem2(unsigned) { retrode3::nesmapper::generated_unsupported(); }
void dumpCHR_M2(unsigned) { retrode3::nesmapper::generated_unsupported(); }
void delay(unsigned) { retrode3::nesmapper::generated_unsupported(); }
} }
}
