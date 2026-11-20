// Strictly read-only NES/NROM adapter for the SINGLE Retrode3 NDJSON host.
// Original pinned Sanni OSCR NES dumpBankPRG/dumpBankCHR loops are compiled
// separately, with only reviewed HAL/storage substitutions. Nothing here
// implements JSON, output files, host CRC/progress, mapper writes or SRAM.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v2.h"
#include "retrode3/read_bus.h"
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


/* NES v2: constant-cost, read-only fingerprint search. The original OSCR
 * catalogue contains Mapper 0 only at this stage. Different game names
 * are not automatically different electrical read layouts. */
std::uint32_t fingerprint(unsigned char const *data)
{
    std::uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < 512; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
bool uniform(unsigned char const *data, unsigned char value)
{
    for (unsigned i = 0; i < 512; ++i)
        if (data[i] != value) return false;
    return true;
}
void candidate_id(std::size_t n, char *out, std::size_t cap)
{
    std::snprintf(out, cap, "nrom-%04u", static_cast<unsigned>(n));
}
void layout_id(unsigned prg, unsigned chr, char *out, std::size_t cap)
{
    std::snprintf(out, cap, "nrom-%u-%u", prg, chr);
}
bool valid_geometry(unsigned prg, unsigned chr)
{
    return (prg == 16 || prg == 32) && (chr == 0 || chr == 8);
}
int fast_probe(retrode3_probe_v2 *out, char *error, size_t cap)
{
    try {
        if (!out || out->struct_size != sizeof(*out))
            return fail(error, cap, "invalid NES v2 probe ABI");
        retrode3::ReadBus bus;
        unsigned char first[512], last[512];
        bus.nes_block(0x8000, first, sizeof(first), retrode3::NesSpace::Prg);
        bus.nes_block(0xe000, last, sizeof(last), retrode3::NesSpace::Prg);
        if ((uniform(first, 0) && uniform(last, 0)) ||
            (uniform(first, 255) && uniform(last, 255)))
            return fail(error, cap, "NES slot empty/unreadable");
        const std::uint32_t a = fingerprint(first), b = fingerprint(last);
        out->candidate_count = out->layout_count = 0;
        for (std::size_t i = 0; i < retrode3::nesstage1::record_count; ++i) {
            retrode3::nesstage1::NromRecord const &record =
                retrode3::nesstage1::records[i];
            if (!((record.id32a && record.id32a == a) ||
                  (record.id32b && record.id32b == b))) continue;
            if (!valid_geometry(record.prg_bytes_kib, record.chr_bytes_kib))
                return fail(error, cap, "invalid pinned NROM catalogue geometry");
            if (out->candidate_count == RETRODE3_MAX_CANDIDATES_V2)
                return fail(error, cap, "too many NES fingerprint candidates");
            retrode3_candidate_v2 &c = out->candidates[out->candidate_count++];
            candidate_id(i, c.id, sizeof(c.id));
            layout_id(record.prg_bytes_kib, record.chr_bytes_kib,
                      c.layout_id, sizeof(c.layout_id));
            std::snprintf(c.title, sizeof(c.title), "%s", record.title);
            c.mapper = c.submapper = 0;
            c.prg_kib = record.prg_bytes_kib;
            c.chr_kib = record.chr_bytes_kib;
            c.payload_crc32 = record.payload_crc32;
            c.read_supported = 1;
            bool found = false;
            for (unsigned n = 0; n < out->layout_count; ++n)
                if (std::strcmp(out->layouts[n].id, c.layout_id) == 0)
                    found = true;
            if (!found) {
                if (out->layout_count == RETRODE3_MAX_LAYOUTS_V2)
                    return fail(error, cap, "too many NES read layouts");
                retrode3_layout_v2 &layout = out->layouts[out->layout_count++];
                std::snprintf(layout.id, sizeof(layout.id), "%s", c.layout_id);
                layout.mapper = layout.submapper = 0;
                layout.prg_kib = c.prg_kib;
                layout.chr_kib = c.chr_kib;
                layout.read_supported = 1;
            }
        }
        std::snprintf(out->identification_status,
                      sizeof(out->identification_status), "%s",
                      !out->candidate_count ? "unknown" :
                      out->candidate_count == 1 ? "unique" : "ambiguous");
        return 0;
    } catch (std::exception const &e) { return fail(error, cap, e.what()); }
    catch (...) { return fail(error, cap, "unknown NES fast-probe error"); }
}

struct PayloadSink {
    const retrode3_host_io_v1 *host;
    std::uint32_t crc;
};
int payload_write(void *context, const void *data, size_t count)
{
    PayloadSink &sink = *static_cast<PayloadSink *>(context);
    if ((!data && count) ||
        sink.host->write(sink.host->context, data, count) != 0) return -1;
    const unsigned char *p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < count; ++i) {
        sink.crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b)
            sink.crc = (sink.crc >> 1) ^ ((sink.crc & 1u) ? 0xedb88320u : 0u);
    }
    return 0;
}
const retrode3::nesstage1::NromRecord *find_record(char const *id)
{
    char idbuf[32];
    for (std::size_t i = 0; i < retrode3::nesstage1::record_count; ++i) {
        candidate_id(i, idbuf, sizeof(idbuf));
        if (std::strcmp(idbuf, id) == 0)
            return &retrode3::nesstage1::records[i];
    }
    return NULL;
}
void synthetic_header(unsigned prg, unsigned chr, unsigned mirroring,
                      unsigned battery, unsigned char *header)
{
    std::memset(header, 0, 16);
    std::memcpy(header, "NES\x1a", 4);
    header[4] = static_cast<unsigned char>(prg / 16u);
    header[5] = static_cast<unsigned char>(chr / 8u);
    header[6] = static_cast<unsigned char>(
        (mirroring == 1 ? 1u : 0u) | (battery ? 2u : 0u) |
        (mirroring == 2 ? 8u : 0u));
}
int read_rom_v2(retrode3_probe_v2 const *probe,
                retrode3_read_request_v2 const *request,
                retrode3_host_io_v1 const *io,
                retrode3_read_result_v2 *result, char *error, size_t cap)
{
    try {
        if (!probe || probe->struct_size != sizeof(*probe) ||
            probe->candidate_count > RETRODE3_MAX_CANDIDATES_V2 ||
            probe->layout_count > RETRODE3_MAX_LAYOUTS_V2 ||
            !request || request->struct_size != sizeof(*request) ||
            !io || io->struct_size < sizeof(*io) ||
            io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
            !io->write || !io->cancelled ||
            !result || result->struct_size != sizeof(*result))
            return fail(error, cap, "invalid NES v2 read ABI");
        unsigned prg = 0, chr = 0;
        const retrode3_candidate_v2 *selected = NULL;
        if (request->manual) {
            if (request->mapper != 0 || request->submapper != 0 ||
                !valid_geometry(request->prg_kib, request->chr_kib) ||
                request->mirroring > 2 || request->battery > 1 ||
                request->candidate_id[0] || request->layout_id[0])
                return fail(error, cap, "unsupported or incomplete manual NROM layout");
            prg = request->prg_kib;
            chr = request->chr_kib;
        } else {
            if (request->candidate_id[0]) {
                for (unsigned i = 0; i < probe->candidate_count; ++i)
                    if (std::strcmp(probe->candidates[i].id,
                                    request->candidate_id) == 0)
                        selected = &probe->candidates[i];
                if (!selected)
                    return fail(error, cap, "candidate no longer matches NES fingerprint");
            }
            const retrode3_layout_v2 *layout = NULL;
            for (unsigned i = 0; i < probe->layout_count; ++i) {
                const retrode3_layout_v2 &entry = probe->layouts[i];
                if (selected && std::strcmp(selected->layout_id, entry.id) != 0)
                    continue;
                if (request->layout_id[0] &&
                    std::strcmp(request->layout_id, entry.id) != 0) continue;
                if (layout)
                    return fail(error, cap, "NES layout selection required");
                layout = &entry;
            }
            if (!layout || !layout->read_supported ||
                layout->mapper != 0 || layout->submapper != 0)
                return fail(error, cap, "unsupported NES read layout");
            prg = layout->prg_kib;
            chr = layout->chr_kib;
        }
        if (!valid_geometry(prg, chr))
            return fail(error, cap, "unsupported NROM PRG/CHR sizes");
        if (io->cancelled(io->context))
            return fail(error, cap, "cancelled");

        PayloadSink sink = {io, 0xffffffffu};
        OSCR::Storage::Shared::setHostSink(payload_write, &sink);
        ClearSink clear;
        OSCR::Cores::NES::dumpBankPRG(0, prg * 1024u, 0x8000);
        if (chr) OSCR::Cores::NES::dumpBankCHR(0, chr * 1024u);
        result->payload_crc32 = ~sink.crc;

        const retrode3::nesstage1::NromRecord *match = NULL;
        char match_id[32] = {};
        for (unsigned i = 0; i < probe->candidate_count; ++i) {
            const retrode3_candidate_v2 &candidate = probe->candidates[i];
            if (candidate.prg_kib != prg || candidate.chr_kib != chr ||
                candidate.payload_crc32 != result->payload_crc32) continue;
            if (match)
                return fail(error, cap, "ambiguous exact NES payload CRC");
            match = find_record(candidate.id);
            if (!match) return fail(error, cap, "invalid NES catalogue candidate ID");
            std::snprintf(match_id, sizeof(match_id), "%s", candidate.id);
        }
        result->verified = match ? 1 : 0;
        if (match) {
            hex_header(match->header_hex, result->header);
            std::snprintf(result->matched_candidate_id,
                          sizeof(result->matched_candidate_id), "%s", match_id);
            std::snprintf(result->title, sizeof(result->title), "%s", match->title);
        } else if (request->manual) {
            synthetic_header(prg, chr, request->mirroring, request->battery,
                             result->header);
            std::snprintf(result->title, sizeof(result->title), "%s",
                          "Unverified NES ROM (manual layout)");
        } else {
            const retrode3::nesstage1::NromRecord *fallback =
                selected ? find_record(selected->id) : NULL;
            // Without an exact CRC, do not invent mirroring/battery metadata:
            // multiple distinct source headers require a manual override.
            for (unsigned i = 0; i < probe->candidate_count; ++i) {
                const retrode3_candidate_v2 &c = probe->candidates[i];
                if (c.prg_kib != prg || c.chr_kib != chr) continue;
                const retrode3::nesstage1::NromRecord *r = find_record(c.id);
                if (!r) return fail(error, cap, "invalid NES catalogue ID");
                if (fallback && std::strcmp(fallback->header_hex, r->header_hex))
                    return fail(error, cap,
                        "CRC mismatch with conflicting headers; use manual layout");
                if (!fallback) fallback = r;
            }
            if (!fallback) return fail(error, cap, "NES layout has no matching record");
            hex_header(fallback->header_hex, result->header);
            std::snprintf(result->title, sizeof(result->title), "%s",
                          "Unverified NES ROM");
        }
        layout_id(prg, chr, result->layout_id, sizeof(result->layout_id));
        return 0;
    } catch (std::exception const &e) { return fail(error, cap, e.what()); }
    catch (...) { return fail(error, cap, "unknown NES v2 read error"); }
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

extern "C" const retrode3_core_operations_v2 *retrode3_core_get_operations_v2(void)
{
    static const retrode3_core_operations_v2 operations = {
        RETRODE3_CORE_OPERATION_ABI_V2, sizeof(retrode3_core_operations_v2),
        fast_probe, read_rom_v2
    };
    return &operations;
}
