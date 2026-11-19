// POSIX/typed Linux read-only bridge for Sanni's original NES bank loops.
// The original upstream code is pinned; these are only hardware/storage shims.
#include "retrode3/oscr_nes_compat.h"
#include "retrode3/read_bus.h"
#include <stdexcept>
namespace OSCR {
namespace Storage { namespace Shared {
std::uint8_t buffer[512] = {};
namespace {
WriteFn writer = 0;
void *writer_context = 0;
}
void setHostSink(WriteFn output, void *context)
{
    if (!output) throw std::runtime_error("NES host callback missing");
    writer = output;
    writer_context = context;
}
void clearHostSink()
{
    writer = 0;
    writer_context = 0;
}
void writeBuffer()
{
    if (!writer || writer(writer_context, buffer, sizeof(buffer)) != 0)
        throw std::runtime_error("NES host output rejected or cancelled");
}
} }
namespace UI { namespace ProgressBar {
void advance(std::size_t) {} // Shared CLI emits real bytes-written progress.
} }
namespace RetroDe3 {
void readPRGBlock(std::uint16_t address, std::uint8_t *out, std::size_t count)
{
    retrode3::ReadBus bus;
    bus.nes_block(address, out, count, retrode3::NesSpace::Prg);
}
void readCHRBlock(std::uint16_t address, std::uint8_t *out, std::size_t count)
{
    retrode3::ReadBus bus;
    bus.nes_block(address, out, count, retrode3::NesSpace::Chr);
}
}
}
