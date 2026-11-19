#pragma once
#include <cstddef>
#include <cstdint>
namespace retrode3 { namespace nesstage1 {
struct NromRecord {
    std::uint32_t id32a;
    std::uint32_t id32b;
    std::uint32_t payload_crc32;
    std::uint16_t prg_bytes_kib;
    std::uint16_t chr_bytes_kib;
    char const *header_hex; // exactly 16 original iNES/NES 2.0 bytes
    char const *title;
};
extern NromRecord const records[];
extern std::size_t const record_count;
} }
