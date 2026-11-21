#ifndef RETRODE3_SMS_MD_TRANSPORT_V1_H
#define RETRODE3_SMS_MD_TRANSPORT_V1_H

// Passive SMS-0007 Eagle schematic, verified nets:
// U$2 A21 and A22 connect SMS /WR and /RD, but the physical MD
// edge uses A22 and A23 (a one-bit shift in the address-bus numbering).
// IMPORTANT: the Eagle 'U$2' pins A00..A22 are adapter LOGICAL names.
// Physical MegaDrive A1..A23 map to those respectively! Thus:
// MD A1..A16 => SMS A0..A15 (all CPU addresses shifted left once).
// MD A17 => SMS /MC-F (high = no RAM access).
// MD A18 => SMS /M8-B (high below 0x8000, low at 0x8000..0xBFFF).
// MD A22 => SMS /WR (MUST remain high during every read).
// MD A23 => SMS /RD (low for the selected ROM read path).
// MD A16 is wired to SMS A15 and /M0-7.
// MD B16 (C_OE) and B17 (C_CE) join SMS /CE on this adapter;
// verify actual mainboard electrical wiring before a production release.
// MD D0..D7 connect directly to SMS D0..D7.
// This mapping is for mode 20 of the Retrode3 7.3 adapter-lane module.
#include <cstddef>
#include <cstdint>

namespace retrode3 { namespace sms {
static const std::uint32_t md_wr_inactive = (1u << 22);
static const std::uint32_t md_mcf_inactive = (1u << 17);
static const std::uint32_t md_m8b_inactive = (1u << 18);
static const std::uint32_t md_rd_active = 0u; // physical MD A23=0
static const std::uint32_t adapter_mode_d0_d7 = 20u;
static const std::uint32_t adapter_mode_sms_mapper_write = 22u;
static const std::uint32_t md_rd_inactive = (1u << 23);

/*
 * Only Sanni's four Sega mapper registers can be sent to the 7.3
 * experimental kernel write primitive. MD CE/WR/RD sequencing is entirely
 * kernel-owned, not recreated in the userspace cartridge core.
 */
inline bool sms_mapper_write_offset(std::uint16_t register_address,
                                    std::uint8_t value,
                                    std::uint32_t *file_offset)
{
    if (!file_offset || register_address < 0xfffcu ||
        register_address > 0xffffu)
        return false;
    if ((register_address == 0xfffcu && value != 0u) ||
        (register_address == 0xfffdu && value != 0u) ||
        (register_address == 0xfffeu && value != 1u) ||
        (register_address == 0xffffu && value >= 64u))
        return false;
    const std::uint32_t address =
        md_rd_inactive | md_wr_inactive | md_mcf_inactive |
        (static_cast<std::uint32_t>(register_address) << 1);
    *file_offset = (adapter_mode_sms_mapper_write << 24) | address;
    return true;
}

inline bool sms_read_offset(std::uint16_t cpu_address, std::size_t size,
                            std::uint32_t *file_offset)
{
    if (!file_offset || size == 0u || size > 512u ||
        cpu_address >= 0xc000u ||
        static_cast<std::uint32_t>(cpu_address) + size > 0xc000u ||
        (cpu_address < 0x8000u &&
         static_cast<std::uint32_t>(cpu_address) + size > 0x8000u))
        return false;
    const std::uint32_t signals = md_wr_inactive | md_rd_active | md_mcf_inactive |
        (cpu_address < 0x8000u ? md_m8b_inactive : 0u);
    // Each SMS address bit is driven by the *next higher* MD address line.
    // Physical MD A0 does not go to the SMS adapter's ROM A0.
    const std::uint32_t md_address =
        signals | (static_cast<std::uint32_t>(cpu_address) << 1);
    // Generic 7.3 kernel ABI: upper byte is mode; lower 24 bits are address.
    *file_offset = (adapter_mode_d0_d7 << 24) | md_address;
    return true;
}
} } // retrode3::sms
#endif
