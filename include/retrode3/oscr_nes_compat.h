#pragma once
#include <cstddef>
#include <cstdint>
namespace OSCR {
namespace RetroDe3 {
void readPRGBlock(std::uint16_t address, std::uint8_t *buffer, std::size_t size);
void readCHRBlock(std::uint16_t address, std::uint8_t *buffer, std::size_t size);
}
namespace Storage { namespace Shared {
extern std::uint8_t buffer[512];
typedef int (*WriteFn)(void *context, void const *bytes, std::size_t size);
void setHostSink(WriteFn write, void *context);
void clearHostSink();
void writeBuffer();
} }
namespace UI {
void printLineSync(const char *message);
namespace ProgressBar {
void advance(std::size_t bytes);
} }
namespace Util {
template <unsigned Base> unsigned power(unsigned exponent) {
    unsigned result = 1;
    while (exponent--) result *= Base;
    return result;
}
}
namespace Cores { namespace NES {
// All hardware calls from generated original switches terminate here.
// Unsupported primitives throw before accessing hardware.
void write_prg_byte(unsigned address, unsigned data);
void write_reg_byte(unsigned address, unsigned data);
void write_ram_byte(unsigned address, unsigned data);
void write_prg_pulsem2(unsigned address, unsigned data);
void write_reg_m59(unsigned address);
void dumpPRG_pulsem2(unsigned base, unsigned address);
void dumpCHR_pulsem2(unsigned address);
void dumpCHR_M2(unsigned address);
std::uint8_t read_prg_byte(unsigned address);
std::uint8_t read_chr_byte(unsigned address);
void delay(unsigned milliseconds);
void generatedReadPRG(unsigned mapper, unsigned prg_exponent);
void generatedReadCHR(unsigned mapper, unsigned chr_exponent);
void dumpPRG(std::uint16_t base, std::uint16_t address);
void dumpCHR(std::uint16_t address);
void dumpBankPRG(std::size_t from, std::size_t to, std::size_t base);
void dumpBankCHR(std::size_t from, std::size_t to);
} }
}
