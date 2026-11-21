#ifndef RETRODE3_SMS_HEADER_V1_H
#define RETRODE3_SMS_HEADER_V1_H

// Pure, read-only SMS cartridge metadata. No GPIO, mapper, power or file I/O.
// This is deliberately separate from the Retrode2 adapter bus transport.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace retrode3 {
namespace sms {

struct Header {
    std::uint32_t declared_rom_bytes;
    std::uint16_t checksum;
    std::uint32_t product_code;
    std::uint8_t revision;
    bool product_code_valid;
    std::uint8_t region_code;
    std::uint8_t size_code;
};

// The actual size table is extracted verbatim from pinned OSCR SMS.cpp.
#include "oscr_sms_size.h"
inline bool decode_size(std::uint8_t code, std::uint32_t *bytes)
{
    if (!bytes) return false;
    // SMS ROM size nibbles; the upstream GG-only 0x3 and fallback are excluded.
    switch (code) {
    case 0xA: case 0xB: case 0xC: case 0xD: case 0xE:
    case 0xF: case 0x0: case 0x1: case 0x2: break;
    default: return false;
    }
    *bytes = oscr_sms_size_hint(code);
    return true;
}

// 'window' contains 16 bytes from a known candidate header address,
// usually SMS CPU $7FF0..$7FFF. Callers must validate the physical bus,
// bank state and selected adapter before reading those bytes.
inline bool decode_header(const std::uint8_t *window, std::size_t length,
                          Header *result)
{
    if (!window || !result || length < 16u ||
        std::memcmp(window, "TMR SEGA", 8u) != 0)
        return false;
    const std::uint8_t size_code = window[15] & 0x0Fu;
    std::uint32_t bytes = 0;
    if (!decode_size(size_code, &bytes)) return false;
    Header parsed = Header();
    parsed.declared_rom_bytes = bytes;
    parsed.checksum = static_cast<std::uint16_t>(
        static_cast<unsigned>(window[10]) |
        (static_cast<unsigned>(window[11]) << 8));
    // Sega header product code: two BCD bytes, least significant two
    // digits first, plus a hexadecimal high digit in bits 7..4 of $7FFE.
    // Source: SMS Power ROM Header. Invalid BCD is not a game identifier.
    const unsigned p0 = window[12], p1 = window[13];
    parsed.product_code_valid =
        (p0 & 15u) < 10u && (p0 >> 4) < 10u &&
        (p1 & 15u) < 10u && (p1 >> 4) < 10u;
    if (parsed.product_code_valid) {
        parsed.product_code = (p0 & 15u) + ((p0 >> 4) * 10u) +
            ((p1 & 15u) * 100u) + ((p1 >> 4) * 1000u) +
            ((window[14] >> 4) * 10000u);
    }
    parsed.revision = static_cast<std::uint8_t>(window[14] & 15u);
    parsed.region_code = static_cast<std::uint8_t>(window[15] >> 4);
    parsed.size_code = size_code;
    *result = parsed;
    return true;
}

} // namespace sms
} // namespace retrode3

#endif