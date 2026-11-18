// Shared POSIX compatibility for ORIGINAL pinned SNES C++ functions.
// Used by acceptance CLI and independently loadable native adapter.
#include "retrode3/oscr_snes_compat.h"
#include "retrode3/read_bus.h"
#include "retrode3-lib.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace OSCR {
namespace Cores {
// These data globals normally live in upstream src/cores/globals.cpp, which
// cannot yet be linked because that translation unit requires Arduino APIs.
std::uint32_t romSize = 0;
std::uint32_t numBanks = 0;
std::uint32_t sramSize = 0;
std::uint16_t romType = 0;
std::uint16_t checksum = 0;
std::uint8_t romVersion = 0;
std::uint8_t const kFileNameMax = 22;
char fileName[22] = {};

std::uint8_t setOutName(char const *src, std::uint8_t limit)
{
    std::size_t n = 0;
    while (n < limit && n < sizeof(fileName) - 1) {
        unsigned char c = static_cast<unsigned char>(src[n]);
        if (c < 32 || c > 126) break;
        fileName[n] = static_cast<char>(c);
        ++n;
    }
    while (n && fileName[n-1] == ' ') --n;
    fileName[n] = '\0';
    return static_cast<std::uint8_t>(n);
}

bool useDefaultName()
{
    std::strcpy(fileName, "SNES-ROM");
    return true;
}
namespace SNES {
std::uint16_t romSpeed = 0;
std::uint16_t romChips = 0;
std::uint8_t cx4Type = 0;
void checkAltConf(std::uint16_t, crc32_t &) {}
}
}
namespace CRC32 {
std::uint32_t calculateCRC(std::uint8_t const *bytes, std::size_t size)
{
    std::uint32_t sum = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        sum ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            sum = (sum >> 1) ^ ((sum & 1u) ? 0xedb88320u : 0u);
    }
    return ~sum;
}
}
namespace Util {
bool isAlphaNumeric(std::uint8_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
           || (c >= 'a' && c <= 'z');
}
void applyTemplate(char *out, std::size_t limit, char const *format,
                   char const *value)
{
    std::snprintf(out, limit, format, value);
}
}
namespace Storage {
void File::write(void const *data, std::size_t count)
{
    if (sink_) {
        if (sink_(sink_context_, data, count) != 0)
            throw std::runtime_error("OSCR host sink refused bytes");
    } else if (!handle_ || std::fwrite(data, 1, count, handle_) != count) {
        throw std::runtime_error("OSCR POSIX storage short write");
    }
    count_ += count;
    if (callback_) callback_(count_, context_);
}
}
namespace UI {
// The first noninteractive runner reports one JSON result upon completion.
// The original OSCR ProgressBar calls remain in the generated C++ source.
void ProgressBar::init(std::uint32_t, unsigned) {}
void ProgressBar::advance(std::uint32_t) {}
void ProgressBar::finish() {}
}
namespace RetroDe3 {
void readBlock(std::uint8_t bank, std::uint16_t address,
               std::uint8_t *buffer, std::size_t count)
{
    retrode3::ReadBus bus;
    // Preserve the kernel chunk size already verified with real cartridges.
    // Do not translate AVR PORT/PIN changes to per-byte Linux syscalls.
    for (std::size_t done = 0; done < count;) {
        std::size_t chunk = count - done;
        if (chunk > 512) chunk = 512;
        bus.snes_block(bank, static_cast<std::uint16_t>(address + done),
                       buffer + done, chunk, retrode3::SnesReadMode::Regular);
        done += chunk;
    }
}
}
}
