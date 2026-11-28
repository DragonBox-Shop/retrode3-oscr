#pragma once
#include <cstdint>
#include <cstddef>
#include "retrode3/core_operations_v1.h"

/* Sanni-derived NES ROM bank selectors. Explicit mapper allowlist and
 * geometry validation; no cartridge save/flash writes. Hardware acceptance
 * of newly enabled development layouts is tracked separately. */
namespace retrode3 { namespace nesmapper {
// Additional reviewed ROM profiles share register primitives, not bank loops.
enum class RegisterFamily { Mmc2, Mmc3, Fme7, Direct, Latch, Expansion, Mmc5, Bandai, Namco, OuterMmc3 };
struct FamilyProfile {
    unsigned mapper;
    const char *label;
    const char *mapping;
    RegisterFamily family;
    unsigned prg_min, prg_max, chr_min, chr_max;
};
const FamilyProfile *family_profiles(std::size_t &count);
const FamilyProfile *family_profile(unsigned mapper);
unsigned family_submapper_mask(unsigned mapper);
bool geometry(unsigned mapper, unsigned submapper, unsigned prg_kib,
              unsigned chr_kib);
bool runtime_writes_enabled();
/* MMC1: battery-backed layouts remain blocked. */
bool mmc1_lab_enabled();
bool mmc1_layout_enabled(unsigned submapper, unsigned prg_kib,
                         unsigned chr_kib, bool battery);
bool runtime_mapper_enabled(unsigned mapper);
void dump(unsigned mapper, unsigned submapper, unsigned prg_kib,
          unsigned chr_kib, const retrode3_host_io_v1 *io,
          bool smd_no_prg_ram = false);
} }
