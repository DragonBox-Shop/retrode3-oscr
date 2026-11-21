#ifndef RETRODE3_SMS_MD_TRANSPORT_V1_H
#define RETRODE3_SMS_MD_TRANSPORT_V1_H

// Passive SMS-0007 Eagle schematic, verified nets:
// MD edge A1..A16 => SMS A0..A15 (the MD cartridge has no A0).
// MD A22 => SMS /WR (high = inactive), A23 => SMS /RD (low = active).
// MD A17 => SMS /MC-F (high = no RAM access).
// MD A18 => SMS /M8-B (high in 0x0000..0x7FFF, low in 0x8000..0xBFFF).
// MD A16 is wired to both SMS A15 and /M0-7.
// The generic MD kernel ABI uses byte addresses: shift every SMS address
// by one bit, including the 8-bit adapter control signals.
// MD OE and CE are both wired to SMS /CE; reads pulse common OE.
// MD D0..D7 connect directly to SMS D0..D7.
// This mapping is for mode 20 of the Retrode3 7.3 adapter-lane module.
#include <cstddef>
#include <cstdint>

namespace retrode3 { namespace sms {
static const std::uint32_t md_wr_inactive = (1u << 22);
static const std::uint32_t md_mcf_inactive = (1u << 17);
static const std::uint32_t md_m8b_inactive = (1u << 18);
static const std::uint32_t md_rd_active = 0u; // A23=0
static const std::uint32_t adapter_mode_d0_d7 = 20u;

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
    const std::uint32_t md_address =
        signals | (static_cast<std::uint32_t>(cpu_address) << 1);
    // Generic 7.3 kernel ABI: upper byte is mode; lower 24 bits are address.
    *file_offset = (adapter_mode_d0_d7 << 24) | md_address;
    return true;
}
} } // retrode3::sms
#endif
