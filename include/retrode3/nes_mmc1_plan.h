#pragma once
#include <cstdint>

/* Pure MMC1 ROM-dump operation plan. NO bus or kernel writes here.
 * In particular, a SerialValue step to $E000 must never be dispatched
 * via the generic NES_MODE_PRG write path: a safe, reviewed atomic kernel
 * operation and physical validation are prerequisites for any execution.
 */
namespace retrode3 { namespace nesmmc1 {

enum StepKind { ResetSerial, SerialValue, ReadPrg, ReadChr };

struct Step {
    StepKind kind;
    std::uint16_t address;
    std::uint32_t value;
    std::uint32_t length;
};

typedef void (*StepCallback)(void *context, Step const &step);

/* Restricted to original Sanni mapper-1 bank geometries (submapper 0).
 * CHR=0 means CHR-RAM, which must never be read as a ROM.
 */
bool geometry(unsigned prg_kib, unsigned chr_kib);

/* Throws std::invalid_argument for unsupported dimensions or null callback.
 * Streams an abstract plan, NOT an executable sequence of GPIO operations.
 */
void plan(unsigned prg_kib, unsigned chr_kib,
          StepCallback callback, void *context);

} }
