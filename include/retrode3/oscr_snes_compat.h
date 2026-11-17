#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

// Small POSIX shims for the original OSCR C++ bank-reader functions.
// They are deliberately separate from upstream; there is no AVR emulation.
namespace OSCR {
namespace Storage {
class File {
public:
    explicit File(std::FILE *handle) : handle_(handle), count_(0) {}
    void write(void const *data, std::size_t count);
    std::size_t bytes_written() const { return count_; }
private:
    std::FILE *handle_;
    std::size_t count_;
};
}
namespace UI {
struct ProgressBar {
    static void init(std::uint32_t maximum, unsigned scale);
    static void advance(std::uint32_t amount);
    static void finish();
};
}
namespace RetroDe3 {
void readBlock(std::uint8_t bank, std::uint16_t address,
               std::uint8_t *buffer, std::size_t count);
}
namespace Cores { namespace SNES {
void readLoRomBanks(std::uint16_t start, std::uint16_t total, Storage::File *file);
void readHiRomBanks(std::uint16_t start, std::uint16_t total, Storage::File *file);
}}
}
