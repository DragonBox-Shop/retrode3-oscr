#include "retrode3/read_bus.h"
#include "retrode3-lib.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace retrode3 {
namespace {
void require_exact(int got, std::size_t expected, char const *where)
{
    if (got < 0 || static_cast<std::size_t>(got) != expected)
        throw std::runtime_error(where);
}
void require_range(bool okay, char const *where)
{
    if (!okay) throw std::out_of_range(where);
}
} // namespace

void ReadBus::nes_block(std::uint16_t address, void *out, std::size_t count, NesSpace space) const
{
    require_range(count <= UINT16_MAX && static_cast<std::uint32_t>(address) + count <= 0x10000u,
                  "NES read crosses address space");
    if (count == 0) return;
    require_range(out != nullptr, "NES output buffer is null");
    require_exact(nes_read(address, out, static_cast<std::uint16_t>(count), static_cast<int>(space)), count,
                  "NES kernel read failed or returned a short read");
}

std::uint8_t ReadBus::nes_byte(std::uint16_t address, NesSpace space) const
{
    std::uint8_t value = 0;
    nes_block(address, &value, sizeof(value), space);
    return value;
}

void ReadBus::md_block(std::uint32_t byte_address, void *out, std::size_t count, MdReadMode mode) const
{
    require_range(count <= UINT32_MAX && static_cast<std::uint64_t>(byte_address) + count <= 0x1000000ull,
                  "MegaDrive read crosses 24-bit address space");
    if (count == 0) return;
    require_range(out != nullptr, "MegaDrive output buffer is null");
    require_exact(md_read(byte_address, out, static_cast<std::uint32_t>(count), static_cast<int>(mode)), count,
                  "MegaDrive kernel read failed or returned a short read");
}

std::uint16_t ReadBus::md_word(std::uint32_t word_address, MdReadMode mode) const
{
    require_range(word_address <= 0x7fffffu, "MegaDrive word address exceeds 24-bit bus");
    std::uint8_t bytes[2] = {};
    md_block(word_address * 2u, bytes, sizeof(bytes), mode);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

void ReadBus::snes_block(std::uint8_t bank, std::uint16_t address, void *out, std::size_t count, SnesReadMode mode) const
{
    require_range(count <= UINT16_MAX && static_cast<std::uint32_t>(address) + count <= 0x10000u,
                  "SNES read crosses bank boundary");
    if (count == 0) return;
    require_range(out != nullptr, "SNES output buffer is null");
    require_exact(snes_read(bank, address, out, static_cast<std::uint16_t>(count), static_cast<int>(mode)), count,
                  "SNES kernel read failed or returned a short read");
}

std::uint8_t ReadBus::snes_byte(std::uint8_t bank, std::uint16_t address, SnesReadMode mode) const
{
    std::uint8_t value = 0;
    snes_block(bank, address, &value, sizeof(value), mode);
    return value;
}
} // namespace retrode3
