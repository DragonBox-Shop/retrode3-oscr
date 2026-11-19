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
namespace UI { namespace ProgressBar {
void advance(std::size_t bytes);
} }
namespace Cores { namespace NES {
void dumpPRG(std::uint16_t base, std::uint16_t address);
void dumpCHR(std::uint16_t address);
void dumpBankPRG(std::size_t from, std::size_t to, std::size_t base);
void dumpBankCHR(std::size_t from, std::size_t to);
} }
}
