// Shared stage-1 MegaDrive ROM probe and read-only byte streaming.
// This is the already physically accepted original-OSCR readWord()/ROM-block
// path, factored out of the legacy CLI for the new dynamic core. Do NOT use
// for 32X, SVP, lock-on, mapper writes, SRAM or other special controls.
#include "retrode3/oscr_md_stage1.h"
#include "retrode3/oscr_md_compat.h"
#include "retrode3/read_bus.h"
#include "retrode3-lib.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace OSCR { namespace RetroDe3 {
uint16_t readWord(uint32_t word_address)
{
    retrode3::ReadBus bus;
    return bus.md_word(word_address, retrode3::MdReadMode::Pulse10);
}
}}


namespace retrode3 {
namespace mdstage1 {
namespace {
std::string text(unsigned char const *data, std::size_t start, std::size_t end)
{
    std::string result;
    for (std::size_t i = start; i < end; ++i) {
        unsigned char c = data[i];
        result += c >= 32 && c < 127 ? static_cast<char>(c) : ' ';
    }
    while (!result.empty() && result[result.size() - 1] == ' ') result.resize(result.size() - 1);
    return result;
}
}

Probe probe()
{
    // These are calls to the upstream C++ readWord(), not an independently
    // rewritten header algorithm. Word addresses become byte addresses in HAL.
    unsigned char header[512] = {};
    for (unsigned i = 0; i < 256; ++i) {
        std::uint16_t word = OSCR::Cores::MegaDrive::readWord(i);
        header[2 * i] = static_cast<unsigned char>(word >> 8);
        header[2 * i + 1] = static_cast<unsigned char>(word);
    }
    if (std::memcmp(header + 0x100, "SEGA", 4) != 0)
        throw std::runtime_error("not a standard SEGA MegaDrive header");
    if (header[0x104] == ' ' && header[0x105] == '3' &&
        header[0x106] == '2' && header[0x107] == 'X')
        throw std::runtime_error("32X cartridge unsupported");
    std::string serial = text(header, 0x180, 0x18e);
    if (serial.compare(0, 10, "GM MK-1563") == 0)
        throw std::runtime_error("Sonic & Knuckles lock-on unsupported");
    if (serial.compare(0, 10, "GM MK-1229") == 0 ||
        serial.compare(0, 9, "GM G-7001") == 0)
        throw std::runtime_error("SVP cartridge unsupported");
    std::uint32_t end = (static_cast<std::uint32_t>(header[0x1a4]) << 24)
        | (static_cast<std::uint32_t>(header[0x1a5]) << 16)
        | (static_cast<std::uint32_t>(header[0x1a6]) << 8)
        | header[0x1a7];
    std::size_t size = static_cast<std::size_t>(end) + 1u;
    if (size < 512 || size > 4194304 || size % 2)
        throw std::runtime_error("unsupported ROM size or banked mapping");
    // Sanni's original sanitization helper, called on the extracted name.
    char filename[22] = {};
    OSCR::Cores::MegaDrive::copyToRomName(
        filename, header + 0x150, static_cast<std::uint8_t>(sizeof(filename) - 1));
    Probe result = {size, (static_cast<unsigned>(header[0x18e]) << 8) | header[0x18f],
                    text(header, 0x150, 0x180), serial, text(header, 0x1f0, 0x200)};
    return result;
}


void read_rom(Probe const &info, std::size_t size, void *context,
              WriteFn write, CancelledFn cancelled)
{
    if (!write) throw std::runtime_error("MegaDrive output callback missing");
    if (size < 512 || size > 4194304 || (size & 1u) || size > info.size)
        throw std::runtime_error("unsupported linear MegaDrive ROM size");
    retrode3::ReadBus bus;
    unsigned char block[2048];
    for (std::size_t offset = 0; offset < size; offset += sizeof(block)) {
        if (cancelled && cancelled(context))
            throw std::runtime_error("cancelled");
        std::size_t count = size - offset;
        if (count > sizeof(block)) count = sizeof(block);
        bus.md_block(static_cast<std::uint32_t>(offset), block, count,
                     retrode3::MdReadMode::Rom);
        if (write(context, block, count) != 0)
            throw std::runtime_error("MegaDrive host output rejected bytes");
    }
}

void close_slot()
{
    md_close();
}
} // namespace mdstage1
} // namespace retrode3
