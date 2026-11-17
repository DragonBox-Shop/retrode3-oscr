// Stage 1 Linux-native runner for ORIGINAL pinned OSCR C++ bank-read functions.
// No original source file is modified. This does not yet port header/CRDB,
// enhancement chip code, SRAM, menus, or the complete upstream SNES core.
#include "retrode3/oscr_snes_compat.h"
#include "retrode3/read_bus.h"
#include "retrode3-lib.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace OSCR {
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
        std::size_t size = 0;
        for (int i = 1; i < argc; ++i) {
            std::string flag = argv[i];
            if ((flag == "--mapping" || flag == "--size-bytes" || flag == "--output")
                && i + 1 < argc) {
                char const *value = argv[++i];
                if (flag == "--mapping") mapping = value;
                else if (flag == "--size-bytes") size = parse_size(value);
                else output = value;
            } else {
                throw std::runtime_error(
                    "usage: oscr-snes-native --mapping lo|hi --size-bytes N --output FILE");
            }
        }
        if ((mapping != "lo" && mapping != "hi") || size == 0 || output.empty())
            throw std::runtime_error(
                "required: --mapping lo|hi --size-bytes N --output FILE");
        dump(mapping, size, output);
        return 0;
    } catch (std::exception const &exc) {
        std::fprintf(stderr, "{\"ok\":false,\"error\":\"%s\"}\n",
                     json_escape(exc.what()).c_str());
        return 2;
    }
}
