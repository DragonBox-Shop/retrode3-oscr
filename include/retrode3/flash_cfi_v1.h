#ifndef RETRODE3_FLASH_CFI_V1_H
#define RETRODE3_FLASH_CFI_V1_H

// Hardware-independent Common Flash Interface (CFI) decoder.
// The caller must enter/exit CFI query mode using an appropriate, explicitly
// selected bus transport. Nothing in this header performs cartridge writes.
// CFI offsets below are BYTE INDICES in the query table, not bus addresses.
// On a 16-bit Mega Drive bus the corresponding flash address is index * 2.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace retrode3 { namespace flash {

struct EraseRegion {
    std::uint64_t start_bytes;
    std::uint32_t block_bytes;
    std::uint32_t blocks;
};

struct CfiGeometry {
    std::uint16_t primary_command_set;
    std::uint16_t alternate_command_set;
    std::uint16_t interface_code;
    std::uint64_t capacity_bytes;
    std::uint32_t write_buffer_bytes;
    std::uint32_t typical_word_us;
    std::uint32_t maximum_word_us;
    std::uint32_t typical_erase_ms;
    std::uint32_t maximum_erase_ms;
    std::vector<EraseRegion> erase_regions;
    CfiGeometry() : primary_command_set(0), alternate_command_set(0),
        interface_code(0), capacity_bytes(0), write_buffer_bytes(0),
        typical_word_us(0), maximum_word_us(0), typical_erase_ms(0),
        maximum_erase_ms(0) {}
};

inline std::uint16_t cfi_u16(const std::uint8_t *p, std::size_t index) {
    return static_cast<std::uint16_t>(p[index] | (static_cast<unsigned>(p[index + 1]) << 8));
}

inline bool cfi_power2(unsigned exponent, std::uint64_t &out) {
    if (exponent > 62) return false;
    out = std::uint64_t(1) << exponent;
    return true;
}

// CFI encodes typical and maximum times as powers of two. The worst-case
// multiplier is also a power of two. Values of zero mean unspecified.
inline std::uint32_t cfi_timeout(unsigned typical, unsigned multiplier) {
    if (typical == 0 || typical == 255 || multiplier == 255 ||
        typical + multiplier > 31) return 0;
    return std::uint32_t(1) << (typical + multiplier);
}

inline bool parse_cfi_v1(const std::uint8_t *p, std::size_t length,
                         CfiGeometry &result, std::string &reason) {
    reason.clear();
    result = CfiGeometry(); // Never expose stale geometry after a failed probe.
    CfiGeometry parsed;
    if (!p || length < 0x2d || p[0x10] != 'Q' ||
        p[0x11] != 'R' || p[0x12] != 'Y') {
        reason = "CFI QRY signature missing or table too short";
        return false;
    }
    parsed.primary_command_set = cfi_u16(p, 0x13);
    parsed.alternate_command_set = cfi_u16(p, 0x17);
    parsed.interface_code = cfi_u16(p, 0x28);
    // Maximum 1 GiB: safely representable in hosts with 32-bit address APIs
    // after bus-specific limits are applied. Reject corrupt exponents.
    if (p[0x27] < 12 || p[0x27] > 30) {
        reason = "CFI device capacity exponent out of range";
        return false;
    }
    parsed.capacity_bytes = std::uint64_t(1) << p[0x27];
    if (p[0x2a] || p[0x2b]) {
        const unsigned n = cfi_u16(p, 0x2a);
        std::uint64_t bytes = 0;
        if (!cfi_power2(n, bytes) ||
            bytes > std::numeric_limits<std::uint32_t>::max()) {
            reason = "CFI write buffer capacity invalid";
            return false;
        }
        parsed.write_buffer_bytes = static_cast<std::uint32_t>(bytes);
    }
    parsed.typical_word_us = cfi_timeout(p[0x1f], 0);
    parsed.maximum_word_us = cfi_timeout(p[0x1f], p[0x23]);
    parsed.typical_erase_ms = cfi_timeout(p[0x21], 0);
    parsed.maximum_erase_ms = cfi_timeout(p[0x21], p[0x25]);
    const unsigned region_count = p[0x2c];
    if (region_count == 0 || region_count > 8 ||
        length < 0x2du + region_count * 4u) {
        reason = "CFI erase region count/table invalid";
        return false;
    }
    std::uint64_t start = 0;
    for (unsigned i = 0; i < region_count; ++i) {
        const std::size_t n = 0x2d + 4 * i;
        const std::uint32_t blocks = static_cast<std::uint32_t>(cfi_u16(p, n)) + 1u;
        const std::uint32_t units = cfi_u16(p, n + 2);
        if (units == 0) {
            reason = "CFI erase region block size unspecified";
            return false;
        }
        const std::uint32_t block_bytes = units * 256u;
        const std::uint64_t length_bytes = static_cast<std::uint64_t>(blocks) * block_bytes;
        if (length_bytes > parsed.capacity_bytes - start) {
            reason = "CFI erase regions exceed reported capacity";
            return false;
        }
        EraseRegion region = {start, block_bytes, blocks};
        parsed.erase_regions.push_back(region);
        start += length_bytes;
    }
    if (start != parsed.capacity_bytes) {
        reason = "CFI erase regions do not cover reported capacity";
        return false;
    }
    result = parsed;
    return true;
}

inline bool erase_plan_v1(const CfiGeometry &geometry, std::uint64_t bytes,
                          std::vector<std::uint64_t> &block_offsets,
                          std::string &reason) {
    block_offsets.clear();
    reason.clear();
    if (bytes == 0 || bytes > geometry.capacity_bytes) {
        reason = "ROM does not fit flash capacity";
        return false;
    }
    // The erase plan covers every entire block touched by the ROM. The
    // eventual writer must verify that every erased byte outside the input
    // is intentionally left 0xFF, never silently preserve old contents.
    for (std::size_t i = 0; i < geometry.erase_regions.size(); ++i) {
        const EraseRegion &r = geometry.erase_regions[i];
        if (!r.block_bytes || !r.blocks) {
            reason = "invalid erase region";
            block_offsets.clear();
            return false;
        }
        for (std::uint32_t j = 0; j < r.blocks; ++j) {
            const std::uint64_t start = r.start_bytes + std::uint64_t(j) * r.block_bytes;
            if (start >= bytes) break;
            block_offsets.push_back(start);
        }
    }
    if (block_offsets.empty()) {
        reason = "empty flash erase plan";
        return false;
    }
    return true;
}

} } // namespace retrode3::flash
#endif
