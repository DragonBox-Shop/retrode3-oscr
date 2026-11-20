#include "retrode3/nes_mmc1_plan.h"
#include <stdexcept>

namespace retrode3 { namespace nesmmc1 {
namespace {
void step(StepCallback emit, void *ctx, StepKind kind,
          std::uint16_t addr, std::uint32_t value, std::uint32_t length)
{
    const Step current = {kind, addr, value, length};
    emit(ctx, current);
}
void reset(StepCallback emit, void *ctx)
{
    step(emit, ctx, ResetSerial, 0x8000u, 0x80u, 0u);
}
void serial(StepCallback emit, void *ctx, std::uint16_t addr, unsigned value)
{
    // Sanni write_mmc1_byte shifts exactly five bits, LSB first.
    // The generic PRG-write executor is only permitted in the explicitly
    // gated no-PRG-WRAM SGROM lab build; normal production remains blocked.
    step(emit, ctx, SerialValue, addr, value & 0x1fu, 5u);
}
}

bool geometry(unsigned prg_kib, unsigned chr_kib)
{
    const bool prg = prg_kib == 32u || prg_kib == 64u ||
                     prg_kib == 128u || prg_kib == 256u ||
                     prg_kib == 512u;
    const bool chr = chr_kib == 0u || chr_kib == 8u ||
                     chr_kib == 16u || chr_kib == 32u ||
                     chr_kib == 64u || chr_kib == 128u;
    return prg && chr;
}

void plan(unsigned prg_kib, unsigned chr_kib,
          StepCallback callback, void *context)
{
    if (!geometry(prg_kib, chr_kib) || !callback)
        throw std::invalid_argument("unsupported MMC1 ROM-only plan");

    if (prg_kib == 32u) {
        // Sanni NES.cpp readPRG mapper 1, NES_PRG==1 special case.
        reset(callback, context);
        step(callback, context, ReadPrg, 0x8000u, 0u, 0x8000u);
    } else {
        const unsigned banks = prg_kib / 16u;
        for (unsigned bank = 0; bank < banks; ++bank) {
            reset(callback, context);
            serial(callback, context, 0x8000u, 0x0cu);
            if (prg_kib == 512u)
                serial(callback, context, 0xa000u,
                       bank >= 16u ? 0x10u : 0u);
            // This $E000 step has a special timing/SRAM-corruption risk.
            // Do not wire this plan to generic kernel register writes.
            serial(callback, context, 0xe000u, bank);
            step(callback, context, ReadPrg, 0x8000u, bank, 0x4000u);
        }
    }

    // Sanni NES.cpp readCHR mapper 1: 8 KiB per bank, 4 KiB bank indices.
    // Never read or write CHR-RAM when chr_kib is zero.
    for (unsigned bank = 0; bank < chr_kib / 8u; ++bank) {
        reset(callback, context);
        serial(callback, context, 0xa000u, bank * 2u);
        step(callback, context, ReadChr, 0x0000u, bank, 0x2000u);
    }
}
} }
