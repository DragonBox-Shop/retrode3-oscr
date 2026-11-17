// Stage 1 Linux-native runner for ORIGINAL pinned OSCR C++ bank-read functions.
// No original source file is modified. This does not yet port header/CRDB,
// enhancement chip code, SRAM, menus, or the complete upstream SNES core.
#include "retrode3/oscr_snes_compat.h"
#include "retrode3/read_bus.h"
#include "retrode3-lib.h"

#include <cctype>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

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
    if (!handle_ || std::fwrite(data, 1, count, handle_) != count)
        throw std::runtime_error("OSCR POSIX storage short write");
    count_ += count;
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

namespace {
std::string json_escape(std::string const &s)
{
    std::string out;
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32) {
            out += '?';
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::size_t parse_size(char const *arg)
{
    if (!arg || *arg == '-' || *arg == '+')
        throw std::runtime_error("invalid --size-bytes");
    errno = 0;
    char *end = nullptr;
    unsigned long size = std::strtoul(arg, &end, 10);
    if (errno || end == arg || *end)
        throw std::runtime_error("invalid --size-bytes");
    if (size < 131072 || size > 4194304)
        throw std::runtime_error("standard SNES size must be 128 KiB to 4 MiB");
    return static_cast<std::size_t>(size);
}

void dump(std::string const &mapping, std::size_t size, std::string const &output)
{
    std::size_t bank_size = mapping == "lo" ? 32768 : 65536;
    if (size % bank_size)
        throw std::runtime_error("ROM size must align to SNES bank size");
    unsigned banks = static_cast<unsigned>(size / bank_size);
    unsigned first = mapping == "lo" ? (size > 3145728 ? 0x80 : 0) : 0xc0;
    if (first + banks > 256)
        throw std::runtime_error("SNES bank selection exceeds bank FF");
    if (::access(output.c_str(), F_OK) == 0)
        throw std::runtime_error("output already exists");

    // Same-directory temp file plus link() gives no-overwrite publication.
    std::string pattern = output + ".partial-XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end());
    temp.push_back('\0');
    int fd = ::mkstemp(temp.data());
    if (fd < 0) throw std::runtime_error("cannot create temporary output");
    std::FILE *file = ::fdopen(fd, "wb");
    if (!file) {
        ::close(fd);
        ::unlink(temp.data());
        throw std::runtime_error("fdopen failed");
    }

    try {
        OSCR::Storage::File writer(file);
        if (mapping == "lo")
            OSCR::Cores::SNES::readLoRomBanks(first, first + banks, &writer);
        else
            OSCR::Cores::SNES::readHiRomBanks(first, first + banks, &writer);
        if (writer.bytes_written() != size)
            throw std::runtime_error("wrong number of ROM bytes");
        if (std::fflush(file) || ::fsync(::fileno(file)))
            throw std::runtime_error("output flush/sync failed");
        if (std::fclose(file)) {
            file = nullptr;
            throw std::runtime_error("output close failed");
        }
        file = nullptr;
        if (::link(temp.data(), output.c_str()) != 0)
            throw std::runtime_error("cannot publish output without overwrite");
        ::unlink(temp.data());
        snes_close();
        std::printf("{\"ok\":true,\"engine\":\"OSCR C++ bank readers\","
                    "\"mapping\":\"%s\",\"bytes_written\":%zu,\"output\":\"%s\"}\n",
                    mapping.c_str(), size, json_escape(output).c_str());
    } catch (...) {
        if (file) std::fclose(file);
        ::unlink(temp.data());
        snes_close();
        throw;
    }
}
}

int main(int argc, char **argv)
{
    try {
        std::string mapping;
        std::string output;
        std::string action = "read-rom";
        std::size_t size = 0;
        for (int i = 1; i < argc; ++i) {
            std::string flag = argv[i];
            if ((flag == "--mapping" || flag == "--size-bytes" ||
                 flag == "--output" || flag == "--action") && i + 1 < argc) {
                char const *value = argv[++i];
                if (flag == "--mapping") mapping = value;
                else if (flag == "--size-bytes") size = parse_size(value);
                else if (flag == "--action") action = value;
                else output = value;
            } else {
                throw std::runtime_error(
                    "usage: oscr-snes-native --action probe|read-rom "
                    "[--mapping lo|hi --size-bytes N] [--output FILE]");
            }
        }

        if (action != "probe" && action != "read-rom")
            throw std::runtime_error("invalid --action");
        // This is the ORIGINAL upstream checkcart() function, compiled from
        // pinned SNES.cpp, with only the 80-byte AVR GPIO loop replaced.
        bool valid = OSCR::Cores::SNES::checkcart();
        if (!valid)
            throw std::runtime_error("original OSCR checkcart() rejected cartridge header");
        unsigned chips = OSCR::Cores::SNES::romChips;
        unsigned mapcode = OSCR::Cores::romType;
        unsigned mbit = OSCR::Cores::romSize;
        if (mapcode > 1)
            throw std::runtime_error("ExHiROM / enhanced mapping unsupported");
        if (chips > 2)
            throw std::runtime_error("special SNES chip unsupported in this Linux stage");
        if (mbit == 0 || mbit > 32)
            throw std::runtime_error("unsupported OSCR SNES header ROM size");
        std::string detected = mapcode == 0 ? "lo" : "hi";
        std::size_t detected_size = static_cast<std::size_t>(mbit) * 131072u;
        if (action == "probe") {
            std::printf("{\"ok\":true,\"engine\":\"OSCR C++ checkcart\","
                        "\"mapping\":\"%s\",\"size_bytes\":%zu,\"chips\":%u,"
                        "\"checksum\":\"%04X\",\"title\":\"%s\"}\n",
                        detected.c_str(), detected_size, chips,
                        OSCR::Cores::checksum,
                        json_escape(OSCR::Cores::fileName).c_str());
            snes_close();
            return 0;
        }
        if (mapping.empty()) mapping = detected;
        if (size == 0) size = detected_size;
        if ((mapping != "lo" && mapping != "hi") || output.empty())
            throw std::runtime_error(
                "read-rom requires --output FILE and a standard LoROM/HiROM mapping");
        // Explicit mapping/size overrides are reserved for known-good
        // experimental comparisons; no cartridge write APIs are present.
        dump(mapping, size, output);
        return 0;
    } catch (std::exception const &exc) {
        snes_close();
        std::fprintf(stderr, "{\"ok\":false,\"error\":\"%s\"}\n",
                     json_escape(exc.what()).c_str());
        return 2;
    }
}
