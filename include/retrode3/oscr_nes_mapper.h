#pragma once
#include <cstdint>
#include "retrode3/core_operations_v1.h"

/* Sanni-derived NES mapper sequences. Register writes are only compiled
 * into explicit test/experimental builds; production remains read-only.
 * Unsupported mapper configurations fail before touching cartridge buses. */
namespace retrode3 { namespace nesmapper {
bool geometry(unsigned mapper, unsigned submapper, unsigned prg_kib,
              unsigned chr_kib);
bool runtime_writes_enabled();
void dump(unsigned mapper, unsigned submapper, unsigned prg_kib,
          unsigned chr_kib, const retrode3_host_io_v1 *io);
} }
