// Strictly read-only NES/NROM adapter for the SINGLE Retrode3 NDJSON host.
// Original pinned Sanni OSCR NES dumpBankPRG/dumpBankCHR loops are compiled
// separately, with only reviewed HAL/storage substitutions. Nothing here
// implements JSON, output files, host CRC/progress, mapper writes or SRAM.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/oscr_nes_compat.h"
#include "retrode3/oscr_nes_stage1.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace {
int fail(char *error, size_t capacity, char const *reason)
{
    if (error && capacity) std::snprintf(error, capacity, "%s", reason);
    return -1;
}

void hex_header(char const *hex, unsigned char *header)
{
    for (unsigned i = 0; i < 16; ++i) {
        int x[2] = {-1, -1};
        for (unsigned j = 0; j < 2; ++j) {
            unsigned char c = static_cast<unsigned char>(hex[i * 2 + j]);
            x[j] = c >= '0' && c <= '9' ? c - '0'
                 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        }
        if (x[0] < 0 || x[1] < 0)
            throw std::runtime_error("invalid pinned iNES header");
        header[i] = static_cast<unsigned char>((x[0] << 4) | x[1]);
    }
    if (std::memcmp(header, "NES\x1a", 4) != 0 ||
        header[4] < 1 || header[4] > 2 || header[5] > 1 ||
        (header[6] & 0xf4) != 0 || (header[7] & 0xf0) != 0)
        throw std::runtime_error("pinned catalog is not plain NES mapper 0");
}

int probe(retrode3_probe_v1 *result, char *error, size_t capacity)
{
    try {
        if (!result || result->struct_size != sizeof(*result))
            return fail(error, capacity, "invalid NES core probe ABI");
        retrode3::nesstage1::Detection found = retrode3::nesstage1::detect();
        retrode3::nesstage1::NromRecord const &r = *found.record;
        result->rom_size_bytes = 16u + (r.prg_bytes_kib + r.chr_bytes_kib) * 1024u;
        std::snprintf(result->title, sizeof(result->title), "%s", r.title);
        std::snprintf(result->mapping, sizeof(result->mapping), "%s", "nrom");
        std::snprintf(result->checksum, sizeof(result->checksum), "%08X",
                      found.payload_crc32);
        return 0;
    } catch (std::exception const &e) {
        return fail(error, capacity, e.what());
    } catch (...) {
        return fail(error, capacity, "unknown NES detection error");
    }
}

struct ClearSink {
    ~ClearSink() { OSCR::Storage::Shared::clearHostSink(); }
};

int read_rom(retrode3_probe_v1 const *info, retrode3_host_io_v1 const *io,
             char *error, size_t capacity)
{
    try {
        if (!info || info->struct_size != sizeof(*info) || !io ||
            io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
            io->struct_size < sizeof(*io) || !io->write || !io->cancelled)
            return fail(error, capacity, "invalid NES core IO ABI");
        if (std::strcmp(info->mapping, "nrom") != 0 ||
            info->rom_size_bytes < 16400 || info->rom_size_bytes > 40976)
            return fail(error, capacity, "unsupported NROM size/mapping");
        if (io->cancelled(io->context))
            return fail(error, capacity, "cancelled");
        retrode3::nesstage1::Detection current = retrode3::nesstage1::detect();
        retrode3::nesstage1::NromRecord const &r = *current.record;
        char title[sizeof(info->title)] = {};
        char checksum[sizeof(info->checksum)] = {};
        std::snprintf(title, sizeof(title), "%s", r.title);
        std::snprintf(checksum, sizeof(checksum), "%08X", current.payload_crc32);
        if (std::strcmp(title, info->title) != 0 ||
            std::strcmp(checksum, info->checksum) != 0 ||
            info->rom_size_bytes != 16u + (r.prg_bytes_kib + r.chr_bytes_kib) * 1024u)
            return fail(error, capacity, "NES cartridge changed since probe");
        unsigned char header[16];
        hex_header(r.header_hex, header);
        if (header[4] * 16u != r.prg_bytes_kib ||
            header[5] * 8u != r.chr_bytes_kib)
            return fail(error, capacity, "NROM header/database size mismatch");

        if (io->write(io->context, header, sizeof(header)) != 0)
            return fail(error, capacity, "NES output header refused or cancelled");
        // Generated ORIGINAL OSCR bank loops now receive 512-byte kernel
        // reads through the typed Linux HAL and stream to the host sink.
        OSCR::Storage::Shared::setHostSink(io->write, io->context);
        ClearSink clear;
        OSCR::Cores::NES::dumpBankPRG(0, r.prg_bytes_kib * 1024u, 0x8000);
        if (r.chr_bytes_kib)
            OSCR::Cores::NES::dumpBankCHR(0, r.chr_bytes_kib * 1024u);
        return 0;
    } catch (std::exception const &e) {
        return fail(error, capacity, e.what());
    } catch (...) {
        return fail(error, capacity, "unknown NES ROM read error");
    }
}

void release()
{
    OSCR::Storage::Shared::clearHostSink();
    retrode3::nesstage1::close_slot();
}
}

extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void)
{
    static const retrode3_core_descriptor_v1 descriptor = {
        RETRODE3_CORE_DISCOVERY_ABI_V1,
        sizeof(retrode3_core_descriptor_v1),
        "nes", "Nintendo Entertainment System", "0.1.0-nrom-stage1", "nes"
    };
    return &descriptor;
}

extern "C" const retrode3_core_operations_v1 *retrode3_core_get_operations_v1(void)
{
    static const retrode3_core_operations_v1 operations = {
        RETRODE3_CORE_OPERATION_ABI_V1, sizeof(retrode3_core_operations_v1),
        probe, read_rom, release
    };
    return &operations;
}
