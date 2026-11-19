// The genuine pinned upstream OSCR MegaDrive readWord/name helpers are
// linked from generated code. This adapter only binds the safe linear
// read-only stage-1 reader to the SHARED host operation ABI. No per-core
// JSON, filesystem output, CRC32, progress, mapper writes or save support.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/oscr_md_stage1.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

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
            return fail(error, capacity, "invalid MegaDrive probe ABI");
        retrode3::mdstage1::Probe info = retrode3::mdstage1::probe();
        result->rom_size_bytes = info.size;
        std::snprintf(result->mapping, sizeof(result->mapping), "%s", "linear");
        std::snprintf(result->title, sizeof(result->title), "%s", info.title.c_str());
        std::snprintf(result->checksum, sizeof(result->checksum), "%04X",
                      info.checksum);
        return 0;
    } catch (std::exception const &e) {
        return fail(error, capacity, e.what());
    } catch (...) {
        return fail(error, capacity, "unknown MegaDrive probe error");
    }
}

int read_rom(retrode3_probe_v1 const *info, retrode3_host_io_v1 const *io,
             char *error, size_t capacity)
{
    try {
        if (!info || info->struct_size != sizeof(*info) || !io ||
            io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
            io->struct_size < sizeof(*io) || !io->write || !io->cancelled)
            return fail(error, capacity, "invalid MegaDrive host IO ABI");
        if (std::strcmp(info->mapping, "linear") != 0 ||
            info->rom_size_bytes < 512u || info->rom_size_bytes > 4194304u ||
            (info->rom_size_bytes & 1u))
            return fail(error, capacity, "unsupported MegaDrive ROM mapping/size");
        if (io->cancelled(io->context))
            return fail(error, capacity, "cancelled");

        // Re-probe before physical streaming: never accept an altered size
        // or identity supplied across the ABI or after cartridge replacement.
        // Only original readWord(), no mapper or RAM control writes.
        retrode3::mdstage1::Probe current = retrode3::mdstage1::probe();
        char checksum[9] = {};
        std::snprintf(checksum, sizeof(checksum), "%04X", current.checksum);
        if (info->rom_size_bytes != current.size ||
            std::strcmp(info->checksum, checksum) != 0 ||
            std::strcmp(info->title, current.title.c_str()) != 0)
            return fail(error, capacity, "MegaDrive cartridge changed since probe");
        retrode3::mdstage1::read_rom(current, current.size, io->context,
                                    io->write, io->cancelled);
        return 0;
    } catch (std::exception const &e) {
        return fail(error, capacity, e.what());
    } catch (...) {
        return fail(error, capacity, "unknown MegaDrive read error");
    }
}

void release()
{
    retrode3::mdstage1::close_slot();
}
} // namespace

extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void)
{
    static const retrode3_core_descriptor_v1 descriptor = {
        RETRODE3_CORE_DISCOVERY_ABI_V1,
        sizeof(retrode3_core_descriptor_v1),
        "md",
        "Mega Drive",
        "0.1.0-stage1",
        "md"
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
