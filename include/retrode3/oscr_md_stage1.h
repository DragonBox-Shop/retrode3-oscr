#pragma once

// Shared read-only MegaDrive stage-1 logic used by both the legacy physical
// acceptance executable and the new dynamically loadable core adapter.
// Sanni's pinned readWord()/copyToRomName() are linked in from the
// generated original-OSCR archive. No writes or mapper controls here.
#include <cstddef>
#include <string>

namespace retrode3 {
namespace mdstage1 {

struct Probe {
    std::size_t size;
    unsigned checksum;
    std::string title;
    std::string serial;
    std::string region;
};

typedef int (*WriteFn)(void *context, void const *data, std::size_t count);
typedef int (*CancelledFn)(void *context);

// Probe only standard direct-mapped MegaDrive ROMs of 512 B..4 MiB.
// Reject 32X, SVP, Sonic & Knuckles lock-on, invalid size and mapping.
Probe probe();

// Byte streaming uses the already physically accepted MD_MODE_ROM kernel
// block reader. The caller owns output storage, CRC32, progress and cancel.
void read_rom(Probe const &info, std::size_t size, void *context,
              WriteFn write, CancelledFn cancelled);

// Always close the physical MegaDrive slot after an operation.
void close_slot();

} // namespace mdstage1
} // namespace retrode3
