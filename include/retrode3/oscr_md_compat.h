#pragma once
#include <stdint.h>

// Partial, read-only host interface for ORIGINAL OSCR C++ MegaDrive.cpp.
// No Arduino register emulation, mapper writes or cartridge write operations.
namespace OSCR {
namespace RetroDe3 {
// The OSCR core passes word addresses, the existing Linux md_read ABI
// expects byte addresses. The HAL also returns an MSB-first word.
uint16_t readWord(uint32_t word_address);
}
namespace Cores { namespace MegaDrive {
uint16_t readWord(uint32_t word_address);
uint8_t copyToRomName(char *output, uint8_t const *input, uint8_t length);
}}
}
