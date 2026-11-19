// Conservative physical NES mapper-0 recognition. NES cartridges have no
// inherent file header or PRG/CHR length; never guess based on bus contents.
// Use Sanni's first/last 512-byte PRG fingerprints, then match full-ROM
// CRC32 for a unique original OSCR database record before reading.
// This stage never writes registers or cartridge memory.
#include "retrode3/oscr_nes_stage1.h"
#include "retrode3/read_bus.h"
#include "retrode3-lib.h"
#include <cstdio>
#include <stdexcept>
#include <cstring>

namespace retrode3 { namespace nesstage1 {
namespace {
std::uint32_t crc32_extend(std::uint32_t sum, unsigned char const *bytes,
                           std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        sum ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            sum = (sum >> 1) ^ ((sum & 1u) ? 0xedb88320u : 0u);
    }
    return sum;
}

std::uint32_t crc32_512(unsigned char const *bytes)
{
    return ~crc32_extend(0xffffffffu, bytes, 512);
}

bool all_value(unsigned char const *bytes, unsigned char value)
{
    for (unsigned i = 0; i < 512; ++i)
        if (bytes[i] != value) return false;
    return true;
}

std::uint32_t payload_crc(unsigned prg_kib, unsigned chr_kib)
{
    retrode3::ReadBus bus;
    std::uint32_t sum = 0xffffffffu;
    unsigned char buffer[512];
    for (unsigned addr = 0x8000; addr < 0x8000u + prg_kib * 1024u; addr += 512) {
        bus.nes_block(static_cast<std::uint16_t>(addr), buffer, sizeof(buffer),
                      retrode3::NesSpace::Prg);
        sum = crc32_extend(sum, buffer, sizeof(buffer));
    }
    for (unsigned addr = 0; addr < chr_kib * 1024u; addr += 512) {
        bus.nes_block(static_cast<std::uint16_t>(addr), buffer, sizeof(buffer),
                      retrode3::NesSpace::Chr);
        sum = crc32_extend(sum, buffer, sizeof(buffer));
    }
    return ~sum;
}
} // namespace

Detection detect()
{
    retrode3::ReadBus bus;
    unsigned char first[512], last[512];
    bus.nes_block(0x8000, first, sizeof(first), retrode3::NesSpace::Prg);
    bus.nes_block(0xe000, last, sizeof(last), retrode3::NesSpace::Prg);
    if ((all_value(first, 0x00) && all_value(last, 0x00)) ||
        (all_value(first, 0xff) && all_value(last, 0xff)))
        throw std::runtime_error("NES slot empty/unreadable");
    std::uint32_t first_hash = crc32_512(first), last_hash = crc32_512(last);
    bool have[3][2] = {};
    std::uint32_t cached[3][2] = {};
    NromRecord const *winner = 0;
    unsigned matches = 0;

    for (std::size_t i = 0; i < record_count; ++i) {
        NromRecord const &r = records[i];
        // Original OSCR NES searchEitherRecord(): either first/last PRG
        // fingerprint is a candidate; full PRG+CHR CRC decides identity.
        if (!((r.id32a && r.id32a == first_hash) ||
              (r.id32b && r.id32b == last_hash)))
            continue;
        unsigned p = r.prg_bytes_kib / 16u;
        unsigned c = r.chr_bytes_kib / 8u;
        if ((p != 1 && p != 2) || c > 1)
            throw std::runtime_error("invalid pinned NES NROM catalog record");
        if (!have[p][c]) {
            cached[p][c] = payload_crc(r.prg_bytes_kib, r.chr_bytes_kib);
            have[p][c] = true;
        }
        if (cached[p][c] != r.payload_crc32) continue;
        winner = &r;
        ++matches;
    }
    if (!winner) throw std::runtime_error("NES ROM not uniquely identified as mapper 0");
    if (matches != 1)
        throw std::runtime_error("ambiguous NROM database match; no ROM dump");
    Detection result = {winner, winner->payload_crc32};
    return result;
}

void close_slot()
{
    nes_close();
}
} } // retrode3::nesstage1
