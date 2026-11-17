#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Arduino source macros used by the original checkcart() algorithm.
// These are host-only replacements; no AVR hardware registers are faked.
#define BUFFN(value) (value), sizeof(value)
#define F(text) (text)
#define PSTR(text) (text)
#define strncmp_P std::strncmp


// Small POSIX shims for the original OSCR C++ bank-reader functions.
// They are deliberately separate from upstream; there is no AVR emulation.
namespace OSCR {
namespace CRC32 {
std::uint32_t calculateCRC(std::uint8_t const *bytes, std::size_t size);
}
namespace Util {
bool isAlphaNumeric(std::uint8_t ch);
void applyTemplate(char *out, std::size_t limit, char const *format,
                   char const *value);
}
namespace Cores {
extern std::uint32_t romSize;
extern std::uint32_t numBanks;
extern std::uint32_t sramSize;
extern std::uint16_t romType;
extern std::uint16_t checksum;
extern std::uint8_t romVersion;
extern std::uint8_t const kFileNameMax;
extern char fileName[22];
std::uint8_t setOutName(char const *src, std::uint8_t size);
bool useDefaultName();
}
namespace Storage {
class File {
public:
    typedef void (*WriteCallback)(std::size_t bytes_written, void *context);
    explicit File(std::FILE *handle)
        : handle_(handle), count_(0), callback_(nullptr), context_(nullptr) {}
    void write(void const *data, std::size_t count);
    std::size_t bytes_written() const { return count_; }
    void on_write(WriteCallback callback, void *context) {
        callback_ = callback;
        context_ = context;
    }
private:
    std::FILE *handle_;
    std::size_t count_;
    WriteCallback callback_;
    void *context_;
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
// The following are the upstream's original LoROM / HiROM / ExHiROM constants.
enum { LO = 0, HI = 1, SA = 3, EX = 4 };
typedef std::uint32_t crc32_t;
extern std::uint16_t romSpeed;
extern std::uint16_t romChips;
extern std::uint8_t cx4Type;
inline std::uint16_t word(std::uint8_t x) { return x; }
// CRDB alternate sizes are not yet linked. The first native stage uses only
// standard, power-of-two carts with valid original OSCR headers.
void checkAltConf(std::uint16_t chksum, crc32_t &identity);
bool checkcart();
void readLoRomBanks(std::uint16_t start, std::uint16_t total, Storage::File *file);
void readHiRomBanks(std::uint16_t start, std::uint16_t total, Storage::File *file);
}}
}
