#pragma once
#include <cstdint>
#include "retrode3/core_operations_v1.h"

/* Sanni-derived NES ROM bank selectors. Explicit mapper allowlist and
 * geometry validation; no cartridge save/flash writes. Hardware acceptance
 * of newly enabled development layouts is tracked separately. */
namespace retrode3 { namespace nesmapper {
bool geometry(unsigned mapper, unsigned submapper, unsigned prg_kib,
              unsigned chr_kib);
bool runtime_writes_enabled();
/* MMC1: battery-backed layouts remain blocked. */
bool mmc1_lab_enabled();
bool mmc1_layout_enabled(unsigned submapper, unsigned prg_kib,
                         unsigned chr_kib, bool battery);
bool runtime_mapper_enabled(unsigned mapper);
void dump(unsigned mapper, unsigned submapper, unsigned prg_kib,
          unsigned chr_kib, const retrode3_host_io_v1 *io);
} }
