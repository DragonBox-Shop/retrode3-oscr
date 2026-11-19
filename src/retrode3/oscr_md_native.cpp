// Stage 1: original OSCR MegaDrive readWord and ROM-name helper in a static
// archive, with a strict read-only linear ROM CLI. This is NOT the full core.
#include "retrode3/oscr_md_stage1.h"
#include "retrode3/read_bus.h"
#include "retrode3-lib.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
std::string escape(std::string const &source)
{
    static const char hex[] = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < source.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(source[i]);
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) {
            result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
        } else result += static_cast<char>(c);
    }
    return result;
}
std::string to_hex(unsigned value, unsigned width)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string s(width, '0');
    for (unsigned i = 0; i < width; ++i) {
        s[width - 1 - i] = hex[value & 15u];
        value >>= 4;
    }
    return s;
}
std::size_t parse_size(char const *arg)
{
    if (!arg || *arg == '-' || *arg == '+' || !*arg)
        throw std::runtime_error("invalid --size-bytes");
    errno = 0;
    char *end = nullptr;
    unsigned long size = std::strtoul(arg, &end, 10);
    if (errno || end == arg || *end || size < 512 || size > 4194304 || (size & 1))
        throw std::runtime_error("linear MegaDrive size must be even and 512 bytes to 4 MiB");
    return static_cast<std::size_t>(size);
}

using retrode3::mdstage1::Probe;
using retrode3::mdstage1::probe;
using retrode3::mdstage1::close_slot;

void emit_probe(Probe const &info)
{
    std::printf("{\"ok\":true,\"engine\":\"OSCR C++ MegaDrive readWord\","
                "\"system\":\"md\",\"header_size_bytes\":%zu,"
                "\"checksum\":\"%s\",\"overseas_title\":\"%s\","
                "\"serial\":\"%s\",\"region\":\"%s\","
                "\"mapping\":\"linear\",\"special_modes\":false}\n",
                info.size, to_hex(info.checksum, 4).c_str(), escape(info.title).c_str(),
                escape(info.serial).c_str(), escape(info.region).c_str());
}

void dump(Probe const &info, std::size_t size, std::string const &output)
{
    if (output.empty() || output.size() > 1024)
        throw std::runtime_error("read-rom requires --output FILE (max 1024 chars)");
    if (size > info.size)
        throw std::runtime_error("size override exceeds detected linear cartridge size");
    if (::access(output.c_str(), F_OK) == 0)
        throw std::runtime_error("output already exists");
    std::string pattern = output + ".partial-XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end());
    temp.push_back('\0');
    int fd = ::mkstemp(temp.data());
    if (fd < 0) throw std::runtime_error("cannot create output temporary file");
    std::FILE *file = ::fdopen(fd, "wb");
    if (!file) {
        ::close(fd); ::unlink(temp.data());
        throw std::runtime_error("fdopen failed");
    }
    try {
        retrode3::ReadBus bus;
        unsigned char block[2048];
        std::uint32_t crc = 0xffffffffu;
        for (std::size_t offset = 0; offset < size; offset += sizeof(block)) {
            std::size_t count = size - offset;
            if (count > sizeof(block)) count = sizeof(block);
            bus.md_block(static_cast<std::uint32_t>(offset), block, count,
                         retrode3::MdReadMode::Rom);
            if (std::fwrite(block, 1, count, file) != count)
                throw std::runtime_error("ROM output write failed");
            for (std::size_t i = 0; i < count; ++i) {
                crc ^= block[i];
                for (unsigned bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
            }
        }
        if (std::fflush(file) || ::fsync(::fileno(file)))
            throw std::runtime_error("output flush failed");
        if (std::fclose(file)) {
            file = nullptr;
            throw std::runtime_error("output close failed");
        }
        file = nullptr;
        if (::link(temp.data(), output.c_str()))
            throw std::runtime_error("cannot publish ROM without overwriting another file");
        ::unlink(temp.data());
        std::printf("{\"ok\":true,\"engine\":\"OSCR C++ MegaDrive partial\","
                    "\"system\":\"md\",\"mapping\":\"linear\","
                    "\"bytes_written\":%zu,\"crc32\":\"%s\","
                    "\"verified\":null,\"output\":\"%s\"}\n",
                    size, to_hex(~crc, 8).c_str(), escape(output).c_str());
    } catch (...) {
        if (file) std::fclose(file);
        ::unlink(temp.data());
        throw;
    }
}
}

int main(int argc, char **argv)
{
    try {
        std::string action = "probe", output;
        std::size_t size = 0;
        for (int i = 1; i < argc; ++i) {
            std::string flag = argv[i];
            if ((flag == "--action" || flag == "--size-bytes" || flag == "--output")
                && i + 1 < argc) {
                char const *value = argv[++i];
                if (flag == "--action") action = value;
                else if (flag == "--size-bytes") size = parse_size(value);
                else output = value;
            } else
                throw std::runtime_error(
                    "usage: oscr-md-native --action probe|read-rom [--output FILE] "
                    "[--size-bytes N]");
        }
        if (action != "probe" && action != "read-rom")
            throw std::runtime_error("unsupported --action");
        if (action == "probe" && (size || !output.empty()))
            throw std::runtime_error("probe takes no output/size options");
        try {
            Probe info = probe();
            if (action == "probe") emit_probe(info);
            else dump(info, size ? size : info.size, output);
            close_slot();
            return 0;
        } catch (...) {
            close_slot();
            throw;
        }
    } catch (std::exception const &exc) {
        close_slot();
        std::fprintf(stderr, "{\"ok\":false,\"error\":\"%s\"}\n",
                     escape(exc.what()).c_str());
        return 2;
    }
}
