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
    bool smd_initialized;
    bool smd_no_prg_ram;
    const retrode3_host_io_v1 *io;
    unsigned cache_base;
    uint8_t cache[512];
    bool cache_valid;
    unsigned register_select;
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
// Conservative, power-of-two geometry bounds, not support for every variant.
// All banking algorithms are generated from the pinned Sanni source.
const FamilyProfile *family_profiles(std::size_t &count)
{
    static const FamilyProfile profiles[] = {
        {9, "MMC2", "mmc2", RegisterFamily::Mmc2, 128, 128, 128, 128},
        {10, "MMC4", "mmc4", RegisterFamily::Mmc2, 128, 256, 64, 128},
        {64, "RAMBO-1", "rambo1", RegisterFamily::Mmc3, 64, 128, 64, 256},
        {69, "Sunsoft FME-7 / 5B", "fme7", RegisterFamily::Fme7, 128, 256, 128, 256},
        {76, "Namco 109", "namco109", RegisterFamily::Mmc3, 128, 128, 128, 128},
        {88, "Namco 118", "namco118", RegisterFamily::Mmc3, 128, 128, 128, 128},
        {95, "Namco 118 (mirroring)", "namco118", RegisterFamily::Mmc3, 128, 128, 32, 32},
        {118, "TxSROM", "txsrom", RegisterFamily::Mmc3, 128, 256, 128, 128},
        {119, "TQROM", "tqrom", RegisterFamily::Mmc3, 128, 128, 64, 64},
        {154, "Namco 118 (single-screen)", "namco118", RegisterFamily::Mmc3, 128, 128, 128, 128},
        {158, "RAMBO-1 (mirroring)", "rambo1", RegisterFamily::Mmc3, 128, 128, 128, 128},
        {18, "Jaleco SS88006", "ss88006", RegisterFamily::Direct, 128, 128, 128, 256},
        {22, "Konami VRC2a", "vrc2a", RegisterFamily::Direct, 128, 128, 128, 128},
        {24, "Konami VRC6a", "vrc6a", RegisterFamily::Direct, 256, 256, 128, 256},
        {26, "Konami VRC6b", "vrc6b", RegisterFamily::Direct, 128, 256, 64, 128},
        {32, "Irem G-101", "g101", RegisterFamily::Direct, 256, 256, 128, 128},
        {33, "Taito TC0190", "tc0190", RegisterFamily::Direct, 128, 256, 128, 256},
        {48, "Taito TC0190/TC0350", "tc0350", RegisterFamily::Direct, 128, 256, 128, 256},
        {65, "Irem H3001", "h3001", RegisterFamily::Direct, 128, 256, 128, 256},
        {68, "Sunsoft 4", "sunsoft4", RegisterFamily::Direct, 128, 128, 128, 256},
        {112, "NTDEC ASDER", "asder", RegisterFamily::Direct, 64, 256, 64, 256},
        {117, "Future Media", "futuremedia", RegisterFamily::Direct, 128, 256, 128, 256},
        {70, "Bandai 74161/32", "bandai-latch", RegisterFamily::Latch, 64, 128, 128, 128},
        {89, "Sunsoft 2", "sunsoft2", RegisterFamily::Latch, 128, 128, 128, 128},
        {152, "Bandai 74161/32 (mirroring)", "bandai-latch", RegisterFamily::Latch, 32, 128, 8, 128},
        {180, "UNROM (fixed first bank)", "unrom-fixed-first", RegisterFamily::Latch, 128, 128, 0, 0},
        {241, "32K latch", "mapper241", RegisterFamily::Latch, 64, 512, 0, 0},
    };
    count = sizeof(profiles) / sizeof(profiles[0]);
    return profiles;
}
const FamilyProfile *family_profile(unsigned mapper)
{
    std::size_t count = 0;
    const FamilyProfile *profiles = family_profiles(count);
    for (std::size_t i = 0; i < count; ++i)
        if (profiles[i].mapper == mapper) return profiles + i;
    return NULL;
}
bool geometry(unsigned mapper, unsigned submapper, unsigned prg, unsigned chr)
{
    if (const FamilyProfile *p = family_profile(mapper))
        return submapper == 0u && power_two(prg) && (chr == 0u || power_two(chr)) &&
            prg >= p->prg_min && prg <= p->prg_max &&
            chr >= p->chr_min && chr <= p->chr_max;
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
    return family_profile(mapper) != NULL || mapper == 1 || mapper == 2 || mapper == 3 ||
           mapper == 4 || mapper == 7 || mapper == 11 || mapper == 13 ||
           mapper == 66 || mapper == 71 || mapper == 206 || mapper == 268;
}

void dump(unsigned mapper, unsigned submapper, unsigned prg_kib,
          unsigned chr_kib, const retrode3_host_io_v1 *io, bool smd_no_prg_ram)
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
    if (smd_no_prg_ram && (mapper != 268u || submapper != 0u ||
                           prg_kib != 512u || chr_kib != 0u))
        throw std::runtime_error("invalid NES no-PRG-RAM board profile");
    GeneratedContext state = {mapper, submapper, false, smd_no_prg_ram, io, 0, {}, false, 0xffu};
    ContextScope scope(state);
    OSCR::Cores::NES::generatedReadPRG(mapper, exponent(prg_kib, 16));
    if (chr_kib)
        OSCR::Cores::NES::generatedReadCHR(mapper, exponent(chr_kib, 4));
}

// Reviewed register addresses and maximum data values for direct ASICs.
// Bank order and values themselves still originate in the generated code.
struct DirectRegisterRule {
    unsigned mapper, address, min_value, max_value;
};
static bool direct_register_allowed(unsigned mapper, unsigned address, unsigned data)
{
    static const DirectRegisterRule rules[] = {
        {18, 0x8000, 0, 15}, {18, 0x8001, 0, 15},
        {18, 0x8002, 0, 15}, {18, 0x8003, 0, 15},
        {18, 0xa000, 0, 15}, {18, 0xa001, 0, 15},
        {22, 0x8000, 0, 15}, {22, 0xb000, 0, 15}, {22, 0xb002, 0, 15},
        {24, 0x8000, 0, 15}, {24, 0xb003, 0, 0}, {24, 0xd000, 0, 255},
        {26, 0x8000, 0, 15}, {26, 0xb003, 0, 0}, {26, 0xd000, 0, 127},
        {32, 0x9000, 1, 1}, {32, 0xa000, 0, 31}, {32, 0xb000, 0, 127},
        {33, 0x8000, 0, 31}, {33, 0x8001, 0, 31}, {33, 0x8002, 0, 127},
        {48, 0x8000, 0, 31}, {48, 0x8001, 0, 31}, {48, 0x8002, 0, 127},
        {65, 0x8000, 0, 31}, {65, 0xb000, 0, 255},
        {68, 0xf000, 0, 7}, {68, 0x8000, 0, 127}, {68, 0x9000, 0, 127},
        {68, 0xa000, 0, 127}, {68, 0xb000, 0, 127},
        {112, 0x8000, 0, 0}, {112, 0x8000, 6, 6}, {112, 0xa000, 0, 255},
        {117, 0x8000, 0, 31}, {117, 0xa000, 0, 255},
    };
    for (const DirectRegisterRule &r : rules)
        if (mapper == r.mapper && address == r.address &&
            data >= r.min_value && data <= r.max_value)
            return true;
    return false;
}
static bool latch_register_allowed(unsigned mapper, unsigned data)
{
    switch (mapper) {
    case 70: case 152: return (data & ~0x7fu) == 0;
    case 89: return (data & ~0xf7u) == 0;
    case 180: return data < 8u;
    case 241: return data < 16u;
    default: return false;
    }
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
            // On the specifically identified board without PRG-RAM,
            // preserve the upstream A001 and both outer register windows.
            // Other profiles keep WRAM disabled and only use their
            // selected window. All bank values remain tightly bounded.
            if (address == 0xa001u && data == 0x80u) {
                prg_register(0xa001u, c.smd_no_prg_ram ? 0x80u : 0u);
                c.smd_initialized = true;
            } else if ((address >= 0x5000u && address <= 0x5003u) ||
                       (address >= 0x6000u && address <= 0x6003u)) {
                if (!c.smd_initialized) unsupported();
                const unsigned reg = address & 3u;
                if ((reg == 0u && data > 3u) ||
                    (reg == 1u && data != 0x60u) ||
                    (reg >= 2u && data != 0u)) unsupported();
                const unsigned window = c.submapper == 0u ? 0x6000u : 0x5000u;
                if (c.smd_no_prg_ram || (address & 0xf000u) == window)
                    cpu_mapper_register(address >= 0x6000u ? CpuRegisterSpace::WramOverlay :
                        CpuRegisterSpace::Expansion, static_cast<uint16_t>(address),
                        static_cast<uint8_t>(data));
            } else {
                if (!c.smd_initialized ||
                    !((address == 0x8000u && data == 6u) ||
                      (address == 0x8001u && data < 64u))) unsupported();
                prg_register(static_cast<uint16_t>(address), static_cast<uint8_t>(data));
            }
            c.cache_valid = false;
            return;
        }
        if (const FamilyProfile *profile = family_profile(c.mapper)) {
            // Reviewed register families. No expansion/WRAM writes,
            // SRAM enable, PPU writes or unrelated command registers.
            switch (profile->family) {
            case RegisterFamily::Mmc2:
                if ((address != 0xa000u && address != 0xb000u && address != 0xc000u) ||
                    data >= (address == 0xa000u ? 16u : 32u)) unsupported();
                break;
            case RegisterFamily::Mmc3:
                if (address == 0xa001u && data == 0x80u) return;
                if (address == 0x8000u) {
                    if (data != 0u && data != 2u && data != 3u && data != 6u)
                        unsupported();
                    c.register_select = data;
                } else if (address == 0x8001u) {
                    if (c.register_select == 0xffu ||
                        (c.mapper == 119u && c.register_select != 6u && data >= 64u))
                        unsupported();
                } else unsupported();
                break;
            case RegisterFamily::Direct:
                if (!direct_register_allowed(c.mapper, address, data)) unsupported();
                if (c.mapper == 112u) {
                    if (address == 0x8000u) c.register_select = data;
                    else if (c.register_select == 0xffu ||
                             (c.register_select == 0u && data >= 32u)) unsupported();
                }
                break;
            case RegisterFamily::Latch:
                // These reviewed discrete boards decode the whole PRG
                // write window. Select an equal ROM byte in the current
                // mapping, never a conflicting fallback address.
                if (address < 0x8000u || !latch_register_allowed(c.mapper, data))
                    unsupported();
                address = safe_register_address(0x8000u, static_cast<uint8_t>(data), c.io);
                break;
            case RegisterFamily::Fme7:
                if (address == 0x8000u) {
                    if (data != 0u && data != 8u && data != 9u) unsupported();
                    c.register_select = data;
                } else if (address == 0xa000u) {
                    if ((c.register_select == 8u && data != 0u) ||
                        (c.register_select == 9u && data >= 32u) ||
                        c.register_select == 0xffu) unsupported();
                } else unsupported();
                break;
            }
        } else if (c.mapper == 4 || c.mapper == 206) {
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
    if ((c.mapper != 2 && c.mapper != 3 && c.mapper != 66 &&
         c.mapper != 70 && c.mapper != 152) || address > 0xffffu ||
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
