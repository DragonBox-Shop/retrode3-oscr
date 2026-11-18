// Original pinned OSCR C++ SNES checkcart and bank-read functions are linked
// separately from generated sources. This adapter contains only the Linux
// operation-ABI bridge and safety gates, never per-core NDJSON/file handling.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/oscr_snes_compat.h"
#include "retrode3-lib.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace {
int fail(char *error, size_t capacity, char const *reason)
{
    if (error && capacity)
        std::snprintf(error, capacity, "%s", reason);
    return -1;
}

int probe(retrode3_probe_v1 *result, char *error, size_t capacity)
{
    try {
        if (!result || result->struct_size != sizeof(*result))
            return fail(error, capacity, "invalid probe ABI");
        if (!OSCR::Cores::SNES::checkcart())
            return fail(error, capacity, "original OSCR SNES checkcart failed");
        unsigned chips = OSCR::Cores::SNES::romChips;
        unsigned mapping = OSCR::Cores::romType;
        unsigned mbit = OSCR::Cores::romSize;
        // Deliberately preserve the original physical acceptance scope.
        // Every enhanced chip and SRAM path must be added separately.
        if (mapping > 1 || chips > 2 || mbit == 0 || mbit > 32)
            return fail(error, capacity, "SNES mapping/chip unsupported in stage 1");
        result->rom_size_bytes = static_cast<uint64_t>(mbit) * 131072u;
        std::snprintf(result->mapping, sizeof(result->mapping),
                      "%s", mapping == 0 ? "lo" : "hi");
        std::snprintf(result->title, sizeof(result->title), "%s",
                      OSCR::Cores::fileName);
        std::snprintf(result->checksum, sizeof(result->checksum), "%04X",
                      OSCR::Cores::checksum);
        return 0;
    } catch (std::exception const &e) {
        return fail(error, capacity, e.what());
    } catch (...) {
        return fail(error, capacity, "unknown OSCR probe error");
    }
}

int read_rom(retrode3_probe_v1 const *info, retrode3_host_io_v1 const *io,
             char *error, size_t capacity)
{
    try {
        if (!info || info->struct_size != sizeof(*info) || !io ||
            io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
            io->struct_size < sizeof(*io) || !io->write || !io->cancelled)
            return fail(error, capacity, "invalid host IO ABI");
        bool lo = std::strcmp(info->mapping, "lo") == 0;
        bool hi = std::strcmp(info->mapping, "hi") == 0;
        if ((!lo && !hi) || info->rom_size_bytes < 131072u ||
            info->rom_size_bytes > 4194304u)
            return fail(error, capacity, "unsupported SNES ROM mapping/size");
        unsigned bank_size = lo ? 32768u : 65536u;
        if (info->rom_size_bytes % bank_size != 0)
            return fail(error, capacity, "invalid SNES bank alignment");
        unsigned banks = static_cast<unsigned>(info->rom_size_bytes / bank_size);
        unsigned first = lo ? (info->rom_size_bytes > 3145728u ? 0x80 : 0) : 0xc0;
        if (first + banks > 256)
            return fail(error, capacity, "SNES bank address out of range");
        if (io->cancelled(io->context))
            return fail(error, capacity, "cancelled");

        OSCR::Storage::File sink(io->write, io->context);
        if (lo)
            OSCR::Cores::SNES::readLoRomBanks(first, first + banks, &sink);
        else
            OSCR::Cores::SNES::readHiRomBanks(first, first + banks, &sink);
        if (sink.bytes_written() != info->rom_size_bytes)
            return fail(error, capacity, "short original SNES ROM dump");
        return 0;
    } catch (std::exception const &e) {
        return fail(error, capacity, e.what());
    } catch (...) {
        return fail(error, capacity, "unknown OSCR read error");
    }
}

void release()
{
    snes_close();
}
}

extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void)
{
    static const retrode3_core_descriptor_v1 descriptor = {
        RETRODE3_CORE_DISCOVERY_ABI_V1,
        sizeof(retrode3_core_descriptor_v1),
        "snes",
        "Super Nintendo",
        "0.1.0-stage1",
        "snes"
    };
    return &descriptor;
}

extern "C" const retrode3_core_operations_v1 *retrode3_core_get_operations_v1(void)
{
    static const retrode3_core_operations_v1 operations = {
        RETRODE3_CORE_OPERATION_ABI_V1,
        sizeof(retrode3_core_operations_v1),
        probe,
        read_rom,
        release
    };
    return &operations;
}
