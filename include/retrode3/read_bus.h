#pragma once

#include <cstddef>
#include <cstdint>

namespace retrode3 {

// Read-only bus operations. No slot power-up or write operations are exposed.
enum class NesSpace : int { Prg = 10, Chr = 11, ChrM2 = 12, Mmc5Sram = 13, Ram = 15, Wram = 16 };
enum class MdReadMode : int { Rom = 0, Pulse10 = 1, Pulse1 = 2, Flash = 4 };
enum class SnesReadMode : int { Regular = 0, HiRom = 9 };

class ReadBus {
public:
    std::uint8_t nes_byte(std::uint16_t address, NesSpace space) const;
    void nes_block(std::uint16_t address, void *out, std::size_t count, NesSpace space) const;
    std::uint16_t md_word(std::uint32_t word_address, MdReadMode mode = MdReadMode::Pulse10) const;
    void md_block(std::uint32_t byte_address, void *out, std::size_t count, MdReadMode mode = MdReadMode::Rom) const;
    std::uint8_t snes_byte(std::uint8_t bank, std::uint16_t address, SnesReadMode mode = SnesReadMode::Regular) const;
    void snes_block(std::uint8_t bank, std::uint16_t address, void *out, std::size_t count, SnesReadMode mode = SnesReadMode::Regular) const;
};

} // namespace retrode3
