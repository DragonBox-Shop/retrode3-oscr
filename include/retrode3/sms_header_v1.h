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
    std::uint8_t region_code;
    std::uint8_t size_code;
};

inline bool decode_size(std::uint8_t code, std::uint32_t *bytes)
{
    if (!bytes) return false;
    switch (code) {
    case 0xA: *bytes = 8u * 1024u; return true;
    case 0xB: *bytes = 16u * 1024u; return true;
    case 0xC: *bytes = 32u * 1024u; return true;
    case 0xD: *bytes = 48u * 1024u; return true;
    case 0xE: *bytes = 64u * 1024u; return true;
    case 0xF: *bytes = 128u * 1024u; return true;
    case 0x0: *bytes = 256u * 1024u; return true;
    case 0x1:
    case 0x2: *bytes = 512u * 1024u; return true;
    default: return false; // No guessed size or GG-specific fallback.
    }
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
    parsed.region_code = static_cast<std::uint8_t>(window[15] >> 4);
    parsed.size_code = size_code;
    *result = parsed;
    return true;
}

} // namespace sms
} // namespace retrode3

#endif
