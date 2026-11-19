#pragma once
#include "retrode3/oscr_nes_catalog.h"
#include <cstdint>

namespace retrode3 { namespace nesstage1 {
struct Detection {
    NromRecord const *record;
    std::uint32_t payload_crc32;
};

// NROM has no physical header. Identify by the SAME first/last PRG
// fingerprint strategy as Sanni, then validate complete PRG+CHR CRC
// against the pinned NES database. Unknown/ambiguous carts fail closed.
Detection detect();
void close_slot();

} }
