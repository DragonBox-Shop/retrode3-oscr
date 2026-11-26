// NES shared core: normal NROM and MMC1 ROM reads, experimental mappers remain gated.
// Original pinned Sanni OSCR NES dumpBankPRG/dumpBankCHR loops are compiled
// separately, with only reviewed HAL/storage substitutions. Nothing here
// implements JSON, output files, host CRC/progress, mapper writes or SRAM.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v2.h"
#include "retrode3/core_operations_v3.h"
#include "retrode3/json_v3.h"
#include "retrode3/read_bus.h"
#include "retrode3/oscr_nes_compat.h"
#include "retrode3/oscr_nes_stage1.h"
#include "retrode3/oscr_nes_mapper.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <map>

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


/* NES v2: constant-cost, read-only fingerprint search over the pinned
 * NROM and MMC1 Sanni catalogue. Mapper writes occur only during read_rom. */
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

bool mmc1_battery(retrode3::nesstage1::Mmc1Record const &r)
{
    const char c = r.header_hex[13]; // low nibble of iNES flag 6
    const unsigned digit = c >= '0' && c <= '9' ? c - '0' :
                           c >= 'A' && c <= 'F' ? c - 'A' + 10 :
                           c >= 'a' && c <= 'f' ? c - 'a' + 10 : 255u;
    if (digit > 15u) throw std::runtime_error("invalid pinned MMC1 flags");
    return (digit & 2u) != 0u;
}
void mmc1_candidate_id(std::size_t index, char *out, std::size_t capacity)
{
    std::snprintf(out, capacity, "mmc1-%04u", static_cast<unsigned>(index));
}
void mmc1_layout_id(unsigned sub, unsigned prg, unsigned chr,
                    char *out, std::size_t capacity)
{
    if (sub == 0)
        std::snprintf(out, capacity, "m1-%u-%u", prg, chr);
    else
        std::snprintf(out, capacity, "m1s%u-%u-%u", sub, prg, chr);
}
// Supplementary cartridge references are CRC/geometry metadata, never ROMs.
// Sanni's pinned data lacks these regional titles. Do not guess fingerprints:
// PAL Rad Racer shares the observed Highway Star probe family; Mario & Yoshi
// has independently measured, stable PRG-bus signatures on real hardware.
// CRC sources: nesCartDB profiles 2124/2698 (Rad Racer EU), NES-RC-USA,
// and profile 1997 (Mario & Yoshi Europe). The PAL Rad Racer CRC was also
// physically matched on this Retrode3 on 2026-11-20.
// Keep Sanni's generated catalogue untouched; IDs remain catalogue-stable.
const retrode3::nesstage1::Mmc1Record regional_mmc1_records[] = {
    {0x402629F3u, 0xF94CC110u, 0x654F4E90u, 128u, 0u, 0u, 0u,
     "4E45531A08001008000000070100001D", "Rad Racer (Europe)"},
    {0u, 0u, 0x8B9D3E9Cu, 128u, 0u, 0u, 0u,
     "4E45531A08001008000000070000001D", "Rad Racer (USA)"},
    // Confirmed on a real PAL cartridge: 5 identical no-write probes
    // ($8000: 29D1D575, $E000: 9A78A49D), followed by a complete
    // 128-KiB PRG + 32-KiB CHR dump: 1406783D / FC0BBA23,
    // combined 836685C4. These are genuine hardware observations.
    {0x29D1D575u, 0x9A78A49Du, 0x836685C4u, 128u, 32u, 0u, 0u,
     "4E45531A080410080000000001000001", "Mario & Yoshi (Europe)"}
};
const std::size_t regional_mmc1_record_count =
    sizeof(regional_mmc1_records) / sizeof(regional_mmc1_records[0]);
std::size_t mmc1_all_record_count()
{
    return retrode3::nesstage1::mmc1_record_count +
           regional_mmc1_record_count;
}
const retrode3::nesstage1::Mmc1Record &mmc1_record(std::size_t index)
{
    if (index < retrode3::nesstage1::mmc1_record_count)
        return retrode3::nesstage1::mmc1_records[index];
    index -= retrode3::nesstage1::mmc1_record_count;
    if (index >= regional_mmc1_record_count)
        throw std::runtime_error("invalid MMC1 reference index");
    return regional_mmc1_records[index];
}
struct BankedRecord {
    unsigned mapper;
    retrode3::nesstage1::Mmc1Record metadata;
};
const BankedRecord banked_records[] = {
#include "oscr_nes_banked_catalog.inc"
};
const std::size_t banked_record_count =
    sizeof(banked_records) / sizeof(banked_records[0]);
std::size_t banked_all_count()
{
    return mmc1_all_record_count() + banked_record_count;
}
unsigned banked_mapper(std::size_t index)
{
    return index < mmc1_all_record_count() ? 1u :
           banked_records[index - mmc1_all_record_count()].mapper;
}
const retrode3::nesstage1::Mmc1Record &banked_record(std::size_t index)
{
    return index < mmc1_all_record_count() ? mmc1_record(index) :
           banked_records[index - mmc1_all_record_count()].metadata;
}
void banked_candidate_id(std::size_t index, char *out, std::size_t cap)
{
    if (index < mmc1_all_record_count()) mmc1_candidate_id(index, out, cap);
    else std::snprintf(out, cap, "banked-%04u",
                       static_cast<unsigned>(index - mmc1_all_record_count()));
}
void banked_layout_id(unsigned mapper, unsigned sub, unsigned prg, unsigned chr,
                      char *out, std::size_t cap)
{
    if (mapper == 1) mmc1_layout_id(sub, prg, chr, out, cap);
    else if (sub) std::snprintf(out, cap, "m%us%u-%u-%u", mapper, sub, prg, chr);
    else std::snprintf(out, cap, "m%u-%u-%u", mapper, prg, chr);
}
// The two read-only 512-byte fingerprints are diagnostic signals,
// not proof of ROM identity. Passing output arguments does not add I/O.
const retrode3::nesstage1::Mmc1Record *find_banked_record(char const *id)
{
    char name[32];
    for (std::size_t i = 0; i < banked_all_count(); ++i) {
        banked_candidate_id(i, name, sizeof(name));
        if (std::strcmp(name, id) == 0)
            return &banked_record(i);
    }
    return NULL;
}
void hex_banked_header(char const *hex, unsigned mapper, unsigned prg, unsigned chr,
                       unsigned sub, unsigned char *header)
{
    if (!hex || std::strlen(hex) != 32u)
        throw std::runtime_error("invalid pinned MMC1 header length");
    for (unsigned i = 0; i < 16u; ++i) {
        unsigned char b = 0;
        for (unsigned j = 0; j < 2u; ++j) {
            const char x = hex[i * 2u + j];
            const unsigned digit = x >= '0' && x <= '9' ? x-'0' :
                    x >= 'A' && x <= 'F' ? x-'A'+10 :
                    x >= 'a' && x <= 'f' ? x-'a'+10 : 255u;
            if (digit > 15u)
                throw std::runtime_error("bad pinned MMC1 header hex");
            b = static_cast<unsigned char>((b << 4) | digit);
        }
        header[i] = b;
    }
    if ((std::memcmp(header, "NES", 3) != 0 || header[3] != 0x1au) ||
        (header[6] >> 4) != mapper || (header[7] & 0xf0u) != 0 ||
        header[4] * 16u != prg || header[5] * 8u != chr ||
        (((header[7] & 0x0cu) == 0x08u) ?
            (header[8] >> 4) : 0u) != sub)
        throw std::runtime_error("pinned MMC1 header/layout mismatch");
}
int fast_probe_with_fingerprints(retrode3_probe_v2 *out,
                                 char *error, size_t cap,
                                 std::uint32_t *first_crc,
                                 std::uint32_t *last_crc)
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
        if (first_crc) *first_crc = a;
        if (last_crc) *last_crc = b;
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
                std::memcpy(layout.id, c.layout_id, sizeof(layout.id));
                layout.mapper = layout.submapper = 0;
                layout.prg_kib = c.prg_kib;
                layout.chr_kib = c.chr_kib;
                layout.read_supported = 1;
            }
        }
        // Pinned Sanni banked mapper fingerprints: read-only samples.
        for (std::size_t i = 0; i < banked_all_count(); ++i) {
            const retrode3::nesstage1::Mmc1Record &rec = banked_record(i);
            const unsigned mapper = banked_mapper(i);
            if (!((rec.id32a && rec.id32a == a) ||
                  (rec.id32b && rec.id32b == b))) continue;
            if (out->candidate_count == RETRODE3_MAX_CANDIDATES_V2)
                return fail(error, cap, "too many NES fingerprint candidates");
            const bool battery = mmc1_battery(rec);
            // Unsupported submapper/geometry and battery-backed layouts
            // are still visible, but never auto-read in this lab.
            const bool supported = !battery &&
                retrode3::nesmapper::runtime_mapper_enabled(mapper) &&
                retrode3::nesmapper::geometry(mapper, rec.submapper,
                    rec.prg_bytes_kib, rec.chr_bytes_kib);
            retrode3_candidate_v2 &c = out->candidates[out->candidate_count++];
            banked_candidate_id(i, c.id, sizeof(c.id));
            banked_layout_id(mapper, rec.submapper, rec.prg_bytes_kib, rec.chr_bytes_kib,
                           c.layout_id, sizeof(c.layout_id));
            std::snprintf(c.title, sizeof(c.title), "%s", rec.title);
            c.mapper = mapper;
            c.submapper = rec.submapper;
            c.prg_kib = rec.prg_bytes_kib;
            c.chr_kib = rec.chr_bytes_kib;
            c.payload_crc32 = rec.payload_crc32;
            c.read_supported = supported ? 1 : 0;
            bool found = false;
            for (unsigned n = 0; n < out->layout_count; ++n) {
                retrode3_layout_v2 &l = out->layouts[n];
                if (std::strcmp(l.id, c.layout_id) != 0) continue;
                // A layout with any battery-backed matching candidate
                // needs manual confirmation; never infer absence of RAM.
                l.read_supported = l.read_supported && c.read_supported;
                found = true;
                break;
            }
            if (!found) {
                if (out->layout_count == RETRODE3_MAX_LAYOUTS_V2)
                    return fail(error, cap, "too many NES read layouts");
                retrode3_layout_v2 &l = out->layouts[out->layout_count++];
                std::memcpy(l.id, c.layout_id, sizeof(l.id));
                l.mapper = mapper;
                l.submapper = rec.submapper;
                l.prg_kib = c.prg_kib;
                l.chr_kib = c.chr_kib;
                l.read_supported = c.read_supported;
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

// Keep the exported NES v2 callback's exact C function-pointer ABI.
int fast_probe(retrode3_probe_v2 *out, char *error, size_t cap)
{
    return fast_probe_with_fingerprints(out, error, cap, NULL, NULL);
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
void synthetic_header(unsigned mapper, unsigned submapper,
                      unsigned prg, unsigned chr, unsigned mirroring,
                      unsigned battery, unsigned char *header)
{
    std::memset(header, 0, 16);
    std::memcpy(header, "NES\x1a", 4);
    header[4] = static_cast<unsigned char>(prg / 16u);
    header[5] = static_cast<unsigned char>(chr / 8u);
    header[6] = static_cast<unsigned char>(((mapper & 15u) << 4) |
        (mirroring == 1 ? 1u : 0u) | (battery ? 2u : 0u) |
        (mirroring == 2 ? 8u : 0u));
    header[7] = static_cast<unsigned char>(mapper & 0xf0u);
    if (submapper) {
        // iNES 1.0 cannot store an NES 2.0 submapper. Preserve it
        // even for an unverified manually configured homebrew ROM.
        if (submapper > 15u)
            throw std::runtime_error("invalid NES submapper");
        header[7] |= 0x08u; // NES 2.0 marker
        header[8] = static_cast<unsigned char>(submapper << 4);
    }
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
        unsigned prg = 0, chr = 0, mapper = 0, submapper = 0;
        const retrode3_candidate_v2 *selected = NULL;
        if (request->manual) {
            if (!(request->mapper == 0 ?
                  request->submapper == 0 &&
                  valid_geometry(request->prg_kib, request->chr_kib) :
                  retrode3::nesmapper::geometry(request->mapper, request->submapper,
                        request->prg_kib, request->chr_kib) &&
                  (request->mapper == 1 ?
                   retrode3::nesmapper::mmc1_layout_enabled(
                       request->submapper, request->prg_kib,
                       request->chr_kib, request->battery != 0) :
                   retrode3::nesmapper::runtime_mapper_enabled(request->mapper))) ||
                request->mirroring > 2 || request->battery > 1 ||
                 (request->mapper != 0 && request->battery) ||
                request->candidate_id[0] || request->layout_id[0])
                return fail(error, cap, "unsupported or incomplete manual NES layout");
            // Battery-backed MMC1 is deliberately withheld until the
            // kernel can guarantee SRAM-safe MMC1 E000 register pulses.
            if (request->mapper == 1 && request->battery)
                return fail(error, cap, "MMC1 battery-backed layout needs SRAM-safe kernel register pulses");
            prg = request->prg_kib;
            chr = request->chr_kib;
            mapper = request->mapper;
            submapper = request->submapper;
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
                (selected && !selected->read_supported))
                return fail(error, cap, "unsupported NES read layout");
            prg = layout->prg_kib;
            chr = layout->chr_kib;
            mapper = layout->mapper;
            submapper = layout->submapper;
        }
        if (!(mapper == 0 && submapper == 0 && valid_geometry(prg, chr)) &&
            !(retrode3::nesmapper::geometry(mapper, submapper, prg, chr) &&
              (mapper == 1 ?
                  retrode3::nesmapper::mmc1_layout_enabled(
                      submapper, prg, chr, request->battery != 0) :
                  retrode3::nesmapper::runtime_mapper_enabled(mapper))))
            return fail(error, cap, "unsupported NES mapper PRG/CHR sizes");
        // A manual "battery=0" switch cannot downgrade an already
        // recognized battery-backed MMC1 board. Its $E000 register
        // strobes may corrupt PRG-RAM even during an otherwise ROM-only
        // read. This check performs no additional cartridge bus writes.
        if (mapper != 0 && request->battery)
            return fail(error, cap, "battery-backed NES bank switching not approved");
        if (mapper != 0) {
            for (unsigned i = 0; i < probe->candidate_count; ++i) {
                const retrode3_candidate_v2 &c = probe->candidates[i];
                if (c.mapper == 0) continue;
                const retrode3::nesstage1::Mmc1Record *known =
                    find_banked_record(c.id);
                if (!known)
                    return fail(error, cap, "invalid NES banked fingerprint candidate");
                if (mmc1_battery(*known))
                    return fail(error, cap,
                        "MMC1 battery-backed fingerprint: SRAM-safe kernel write required");
            }
        }
        if (io->cancelled(io->context))
            return fail(error, cap, "cancelled");

        PayloadSink sink = {io, 0xffffffffu};
        OSCR::Storage::Shared::setHostSink(payload_write, &sink);
        ClearSink clear;
        if (mapper == 0) {
            OSCR::Cores::NES::dumpBankPRG(0, prg * 1024u, 0x8000);
            if (chr) OSCR::Cores::NES::dumpBankCHR(0, chr * 1024u);
        } else {
            retrode3::nesmapper::dump(mapper, submapper, prg, chr, io);
        }
        result->payload_crc32 = ~sink.crc;

        if (mapper != 0) {
            // Full payload CRC can identify even Sanni catalogue entries
            // that have no 512-byte probe fingerprint. No extra bus reads.
            const retrode3::nesstage1::Mmc1Record *exact = NULL;
            unsigned exact_count = 0;
            std::size_t exact_index = 0;
            for (std::size_t i = 0;
                 i < banked_all_count(); ++i) {
                const retrode3::nesstage1::Mmc1Record &r = banked_record(i);
                if (banked_mapper(i) != mapper ||
                    r.prg_bytes_kib != prg || r.chr_bytes_kib != chr ||
                    r.submapper != submapper ||
                    r.payload_crc32 != result->payload_crc32) continue;
                if (exact && std::strcmp(exact->header_hex, r.header_hex))
                    return fail(error, cap,
                                "multiple NES CRC matches with different headers");
                if (!exact) { exact = &r; exact_index = i; }
                ++exact_count;
            }
            if (exact) {
                hex_banked_header(exact->header_hex, mapper, prg, chr, submapper,
                                result->header);
                result->verified = exact_count == 1u ? 1u : 0u;
                if (exact_count == 1u) {
                    banked_candidate_id(exact_index, result->matched_candidate_id,
                                      sizeof(result->matched_candidate_id));
                    std::snprintf(result->title, sizeof(result->title),
                                  "%s", exact->title);
                } else {
                    std::snprintf(result->title, sizeof(result->title), "%s",
                                  "Ambiguous NES ROM (matching payload)");
                }
            } else if (request->manual) {
                synthetic_header(mapper, submapper, prg, chr, request->mirroring,
                                 request->battery, result->header);
                std::snprintf(result->title, sizeof(result->title), "%s",
                              "Unverified NES ROM (manual layout)");
            } else {
                const retrode3::nesstage1::Mmc1Record *fallback = NULL;
                if (selected) fallback = find_banked_record(selected->id);
                for (unsigned i = 0; i < probe->candidate_count; ++i) {
                    const retrode3_candidate_v2 &c = probe->candidates[i];
                    if (c.mapper != mapper || c.submapper != submapper ||
                        c.prg_kib != prg || c.chr_kib != chr) continue;
                    const retrode3::nesstage1::Mmc1Record *r =
                        find_banked_record(c.id);
                    if (!r) return fail(error, cap,
                                        "invalid NES catalogue candidate ID");
                    if (fallback &&
                        std::strcmp(fallback->header_hex, r->header_hex))
                        return fail(error, cap,
                            "unverified NES layout has conflicting headers; use manual");
                    if (!fallback) fallback = r;
                }
                if (!fallback)
                    return fail(error, cap, "NES layout without matching record");
                hex_banked_header(fallback->header_hex, mapper, prg, chr, submapper,
                                result->header);
                std::snprintf(result->title, sizeof(result->title), "%s",
                              "Unverified NES ROM");
            }
            banked_layout_id(mapper, submapper, prg, chr, result->layout_id,
                           sizeof(result->layout_id));
            return 0;
        }


        const retrode3::nesstage1::NromRecord *match = NULL;
        char match_id[32] = {};
        for (unsigned i = 0; mapper == 0 && i < probe->candidate_count; ++i) {
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
            synthetic_header(mapper, submapper, prg, chr, request->mirroring,
                             request->battery, result->header);
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
        if (mapper == 0)
            layout_id(prg, chr, result->layout_id, sizeof(result->layout_id));
        else
            std::snprintf(result->layout_id, sizeof(result->layout_id),
                          "m%u-%u-%u", mapper, prg, chr);
        return 0;
    } catch (std::exception const &e) { return fail(error, cap, e.what()); }
    catch (...) { return fail(error, cap, "unknown NES v2 read error"); }
}

/* Retrofit v3 in this Retrode3-owned adapter. The pinned Sanni banks
 * and v2 read/verification routines remain unchanged. */
int capabilities_json_v3(char *out, size_t len, char *err, size_t cap)
{
    static const char schema[] = R"V3({
"schema_version":1,"schema_id":"nes-layout-v1",
"hardware_access":{"version":1,
 "probe":{"module_parameters":{"legacy_bridge_hiz_test":false,"sms_mapper_write_test":false}},
 "read":{"module_parameters":{"legacy_bridge_hiz_test":false,"sms_mapper_write_test":false}}},
"selection_modes":["auto","candidate","layout","override"],
"fields":[
{"id":"mapper","label":"Mapper","type":"enum","required":true,
 "values":[{"value":0,"label":"NROM","read_supported":true},
           {"value":1,"label":"MMC1","read_supported":true},
           {"value":2,"label":"UxROM","read_supported":true},
           {"value":3,"label":"CNROM","read_supported":true},
           {"value":4,"label":"MMC3","read_supported":true},
           {"value":7,"label":"AxROM","read_supported":true}]},
{"id":"submapper","label":"Submapper","type":"integer","minimum":0,"maximum":15,"required":true,"default":0},
{"id":"prg_kib","label":"PRG ROM (KiB)","type":"integer","minimum":16,"maximum":512,"required":true},
{"id":"chr_kib","label":"CHR ROM (KiB)","type":"enum","required":true,"values":[0,8,16,32,64,128,256]},
{"id":"mirroring","label":"Mirroring","type":"enum","required":true,
 "values":["horizontal","vertical","four_screen"]},
{"id":"battery","label":"Battery","type":"boolean","required":true,"default":false}],
"rules":[
{"when":{"mapper":0},"allow":{"submapper":[0],"prg_kib":[16,32],"chr_kib":[0,8]},"read_supported":true},
{"when":{"mapper":1,"submapper":0},"allow":{"prg_kib":[32,64,128,256,512],"chr_kib":[0,8,16,32,64,128]},"read_supported":true},
{"when":{"mapper":1,"submapper":5},"allow":{"prg_kib":[32],"chr_kib":[8,16,32,64,128]},"read_supported":true},
{"when":{"mapper":1,"battery":true},"read_supported":false,"reason":"SRAM-safe MMC1 pulses not approved"},
{"when":{"mapper":2},"allow":{"submapper":[0,1,2],"prg_kib":[64,128,256],"chr_kib":[0]},"read_supported":true},
{"when":{"mapper":3},"allow":{"submapper":[0,1,2],"prg_kib":[16,32],"chr_kib":[8,16,32]},"read_supported":true},
{"when":{"mapper":4},"allow":{"submapper":[0],"prg_kib":[32,64,128,256,512],"chr_kib":[0,8,16,32,64,128,256]},"read_supported":true},
{"when":{"mapper":7},"allow":{"submapper":[0,1,2],"prg_kib":[64,128,256],"chr_kib":[0]},"read_supported":true},
{"when":{"mapper":2,"battery":true},"read_supported":false,"reason":"Battery-backed bank switching not approved"},
{"when":{"mapper":3,"battery":true},"read_supported":false,"reason":"Battery-backed bank switching not approved"},
{"when":{"mapper":4,"battery":true},"read_supported":false,"reason":"Battery-backed bank switching not approved"},
{"when":{"mapper":7,"battery":true},"read_supported":false,"reason":"Battery-backed bank switching not approved"}]
})V3";
    if (std::strlen(schema) + 1 > len)
        return fail(err, cap, "NES v3 capability buffer too small");
    std::memcpy(out, schema, sizeof(schema));
    return 0;
}

int fast_probe_json_v3(char *out, size_t len, char *err, size_t cap)
{
    retrode3_probe_v2 result;
    std::memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    std::uint32_t first_crc = 0, last_crc = 0;
    if (fast_probe_with_fingerprints(&result, err, cap, &first_crc, &last_crc)) return -1;
    const bool unique_layout = result.layout_count == 1;
    const std::string ambiguity = result.candidate_count < 2 ? "none" :
        (unique_layout ? "titles_only" : "layouts");
    const bool required = result.layout_count != 1 ||
        !result.layouts[0].read_supported;
    std::string json = std::string("{\"identification_status\":") +
        retrode3::json_quote_v3(result.identification_status) +
        ",\"ambiguity\":" + retrode3::json_quote_v3(ambiguity) +
        ",\"system_detected\":true" +
        ",\"selection_required\":" + (required ? "true" : "false") +
        ",\"candidate_lookup\":\"fingerprint_only\",\"checksum\":null" +
        ",\"probe_fingerprints\":{\"cpu_8000_crc32\":" +
            retrode3::json_quote_v3(
                [&]() { char h[9]; std::snprintf(h,sizeof(h),"%08X",first_crc); return std::string(h); }()) +
        ",\"cpu_e000_crc32\":" +
            retrode3::json_quote_v3(
                [&]() { char h[9]; std::snprintf(h,sizeof(h),"%08X",last_crc); return std::string(h); }()) + "}" +
        ",\"mapping\":" + retrode3::json_quote_v3(unique_layout ?
            (result.layouts[0].mapper == 0 ? "nrom" :
             result.layouts[0].mapper == 1 ? "mmc1" :
             result.layouts[0].mapper == 2 ? "uxrom" :
             result.layouts[0].mapper == 3 ? "cnrom" :
             result.layouts[0].mapper == 4 ? "mmc3" :
             result.layouts[0].mapper == 7 ? "axrom" : "unknown") : "unknown") +
        ",\"size_bytes\":" + std::to_string(unique_layout ?
            16u + (result.layouts[0].prg_kib + result.layouts[0].chr_kib) * 1024u : 0u) +
        ",\"title\":" +
        retrode3::json_quote_v3(result.candidate_count == 1 ?
                                     result.candidates[0].title : "NES cartridge") +
        ",\"candidates\":[";
    for (unsigned i = 0; i < result.candidate_count; ++i) {
        const retrode3_candidate_v2 &c = result.candidates[i];
        if (i) json += ",";
        char crc[9];
        std::snprintf(crc, sizeof(crc), "%08X", c.payload_crc32);
        json += "{\"id\":" + retrode3::json_quote_v3(c.id)
            + ",\"title\":" + retrode3::json_quote_v3(c.title)
            + ",\"layout_id\":" + retrode3::json_quote_v3(c.layout_id)
            + ",\"mapper\":" + std::to_string(c.mapper)
            + ",\"submapper\":" + std::to_string(c.submapper)
            + ",\"prg_kib\":" + std::to_string(c.prg_kib)
            + ",\"chr_kib\":" + std::to_string(c.chr_kib)
            + ",\"payload_crc32_expected\":" + retrode3::json_quote_v3(crc)
            + ",\"read_supported\":" + (c.read_supported ? "true" : "false")
            + (c.read_supported ? "" : ",\"reason\":\"Hardware read not approved\"")
            + "}";
    }
    json += "],\"read_layouts\":[";
    for (unsigned i = 0; i < result.layout_count; ++i) {
        const retrode3_layout_v2 &l = result.layouts[i];
        if (i) json += ",";
        json += "{\"id\":" + retrode3::json_quote_v3(l.id)
            + ",\"label\":" + retrode3::json_quote_v3(l.id)
            + ",\"mapper\":" + std::to_string(l.mapper)
            + ",\"submapper\":" + std::to_string(l.submapper)
            + ",\"prg_kib\":" + std::to_string(l.prg_kib)
            + ",\"chr_kib\":" + std::to_string(l.chr_kib)
            + ",\"read_supported\":" + (l.read_supported ? "true" : "false")
            + "}";
    }
    json += "]}";
    if (json.size() + 1 > len)
        return fail(err, cap, "NES v3 probe buffer too small");
    std::memcpy(out, json.c_str(), json.size() + 1);
    return 0;
}

bool v3_parameter(const retrode3_selection_v3 *choice, const char *key,
                  uint32_t type, uint32_t &num, std::string &str)
{
    for (unsigned i = 0; i < choice->parameter_count; ++i) {
        const retrode3_parameter_v3 &p = choice->parameters[i];
        if (std::strcmp(p.id, key) != 0) continue;
        if (p.type != type) throw std::runtime_error("invalid NES override parameter type");
        num = p.integer_value;
        if (type == RETRODE3_PARAMETER_STRING_V3) str = p.string_value;
        return true;
    }
    return false;
}

void selection_to_nes_v3(const retrode3_selection_v3 *choice,
                         const retrode3_probe_v2 &probe,
                         retrode3_read_request_v2 &request,
                         unsigned &prg, unsigned &chr)
{
    if (!choice || choice->struct_size != sizeof(*choice) ||
        choice->protocol_version != 1 ||
        std::strcmp(choice->schema_id, "nes-layout-v1") != 0 ||
        choice->parameter_count > RETRODE3_MAX_PARAMETERS_V3)
        throw std::runtime_error("invalid or stale NES selection");
    std::memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    const retrode3_layout_v2 *layout = NULL;
    const retrode3_candidate_v2 *candidate = NULL;
    if (choice->mode == RETRODE3_SELECT_OVERRIDE_V3) {
        if (choice->candidate_id[0] || choice->layout_id[0] ||
            choice->parameter_count != 6)
            throw std::runtime_error("manual NES override needs exactly six fields");
        uint32_t val = 0;
        std::string str;
        if (!v3_parameter(choice, "mapper", RETRODE3_PARAMETER_INTEGER_V3, val, str))
            throw std::runtime_error("missing mapper");
        request.mapper = val;
        if (!v3_parameter(choice, "submapper", RETRODE3_PARAMETER_INTEGER_V3, val, str))
            throw std::runtime_error("missing submapper");
        request.submapper = val;
        if (!v3_parameter(choice, "prg_kib", RETRODE3_PARAMETER_INTEGER_V3, val, str))
            throw std::runtime_error("missing prg_kib");
        request.prg_kib = val;
        if (!v3_parameter(choice, "chr_kib", RETRODE3_PARAMETER_INTEGER_V3, val, str))
            throw std::runtime_error("missing chr_kib");
        request.chr_kib = val;
        if (!v3_parameter(choice, "battery", RETRODE3_PARAMETER_BOOLEAN_V3, val, str))
            throw std::runtime_error("missing battery");
        request.battery = static_cast<uint8_t>(val);
        if (!v3_parameter(choice, "mirroring", RETRODE3_PARAMETER_STRING_V3, val, str))
            throw std::runtime_error("missing mirroring");
        if (str != "horizontal" && str != "vertical" && str != "four_screen")
            throw std::runtime_error("invalid NES mirroring override");
        request.mirroring = str == "vertical" ? 1 : str == "four_screen" ? 2 : 0;
        request.manual = 1;
        const bool geometry = request.mapper == 0 ?
            request.submapper == 0 && valid_geometry(request.prg_kib, request.chr_kib) :
            retrode3::nesmapper::geometry(request.mapper, request.submapper,
                                          request.prg_kib, request.chr_kib) &&
            (request.mapper == 1 ?
             retrode3::nesmapper::mmc1_layout_enabled(request.submapper,
                request.prg_kib, request.chr_kib, request.battery != 0) :
             retrode3::nesmapper::runtime_mapper_enabled(request.mapper));
        if (!geometry || (request.mapper != 0 && request.battery))
            throw std::runtime_error("NES override geometry or mapper not approved");
        prg = request.prg_kib; chr = request.chr_kib;
        return;
    }
    if (choice->parameter_count || (choice->mode == RETRODE3_SELECT_AUTO_V3 &&
        (choice->candidate_id[0] || choice->layout_id[0])))
        throw std::runtime_error("unexpected NES selection parameters");
    if (choice->mode == RETRODE3_SELECT_CANDIDATE_V3) {
        if (!choice->candidate_id[0] || choice->layout_id[0])
            throw std::runtime_error("invalid candidate selection");
        for (unsigned i = 0; i < probe.candidate_count; ++i)
            if (std::strcmp(choice->candidate_id, probe.candidates[i].id) == 0)
                candidate = &probe.candidates[i];
        if (!candidate || !candidate->read_supported)
            throw std::runtime_error("NES candidate not approved or no longer present");
        std::snprintf(request.candidate_id, sizeof(request.candidate_id), "%s",
                      candidate->id);
    } else if (choice->mode == RETRODE3_SELECT_LAYOUT_V3) {
        if (!choice->layout_id[0] || choice->candidate_id[0])
            throw std::runtime_error("invalid layout selection");
        if (std::strlen(choice->layout_id) >= sizeof(request.layout_id))
            throw std::runtime_error("NES layout ID too long");
        std::memcpy(request.layout_id, choice->layout_id,
                    std::strlen(choice->layout_id) + 1);
    } else if (choice->mode != RETRODE3_SELECT_AUTO_V3)
        throw std::runtime_error("unknown NES selection mode");
    for (unsigned i = 0; i < probe.layout_count; ++i) {
        const retrode3_layout_v2 &l = probe.layouts[i];
        if (candidate && std::strcmp(l.id, candidate->layout_id) != 0) continue;
        if (choice->mode == RETRODE3_SELECT_LAYOUT_V3 &&
            std::strcmp(l.id, choice->layout_id) != 0) continue;
        if (layout) throw std::runtime_error("NES layout choice required");
        layout = &l;
    }
    if (!layout || !layout->read_supported)
        throw std::runtime_error("NES layout unavailable or not approved");
    const bool geometry = layout->mapper == 0 ?
        layout->submapper == 0 && valid_geometry(layout->prg_kib, layout->chr_kib) :
        retrode3::nesmapper::geometry(layout->mapper, layout->submapper,
                                      layout->prg_kib, layout->chr_kib) &&
        (layout->mapper == 1 ?
         retrode3::nesmapper::mmc1_layout_enabled(layout->submapper,
                layout->prg_kib, layout->chr_kib, false) :
         retrode3::nesmapper::runtime_mapper_enabled(layout->mapper));
    if (!geometry) throw std::runtime_error("unsupported NES read layout");
    prg = layout->prg_kib; chr = layout->chr_kib;
}

int plan_read_v3(const retrode3_selection_v3 *choice,
                 retrode3_read_plan_v3 *plan, char *err, size_t cap)
{
    try {
        if (!plan || plan->struct_size != sizeof(*plan))
            return fail(err, cap, "invalid NES v3 read plan ABI");
        retrode3_probe_v2 probe_info;
        std::memset(&probe_info, 0, sizeof(probe_info));
        probe_info.struct_size = sizeof(probe_info);
        if (fast_probe(&probe_info, err, cap)) return -1;
        retrode3_read_request_v2 request;
        unsigned prg = 0, chr = 0;
        selection_to_nes_v3(choice, probe_info, request, prg, chr);
        plan->prefix_bytes = 16;
        plan->payload_bytes = (prg + chr) * 1024ull;
        return 0;
    } catch (const std::exception &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "invalid NES v3 read plan"); }
}

int read_rom_v3(const retrode3_selection_v3 *choice,
                const retrode3_host_io_v1 *io, retrode3_read_result_v3 *result,
                char *err, size_t cap)
{
    try {
        if (!result || result->struct_size != sizeof(*result))
            return fail(err, cap, "invalid NES v3 result ABI");
        retrode3_probe_v2 probe_info;
        std::memset(&probe_info, 0, sizeof(probe_info));
        probe_info.struct_size = sizeof(probe_info);
        if (fast_probe(&probe_info, err, cap)) return -1;
        retrode3_read_request_v2 request;
        unsigned prg = 0, chr = 0;
        selection_to_nes_v3(choice, probe_info, request, prg, chr);
        retrode3_read_result_v2 old;
        std::memset(&old, 0, sizeof(old));
        old.struct_size = sizeof(old);
        if (read_rom_v2(&probe_info, &request, io, &old, err, cap))
            return -1;
        std::memcpy(result->prefix, old.header, 16);
        result->prefix_bytes = 16;
        result->payload_crc_known = 1;
        result->payload_crc32 = old.payload_crc32;
        const std::string title(old.title);
        const std::string matched(old.matched_candidate_id);
        const std::string selected(choice->candidate_id);
        const bool mismatch = !selected.empty() && matched != selected;
        const std::string status = mismatch ? "mismatch" :
            old.verified ? "verified" :
            matched.empty() ? "not_found" : "ambiguous";
        unsigned actual_mapper = request.mapper;
        unsigned actual_submapper = request.submapper;
        if (!request.manual) {
            for (unsigned i = 0; i < probe_info.layout_count; ++i) {
                const retrode3_layout_v2 &l = probe_info.layouts[i];
                if (std::strcmp(old.layout_id, l.id) != 0) continue;
                actual_mapper = l.mapper;
                actual_submapper = l.submapper;
                break;
            }
        }
        char crc[9];
        std::snprintf(crc, sizeof(crc), "%08X", old.payload_crc32);
        std::string expected = "null";
        if (!selected.empty()) {
            for (unsigned i = 0; i < probe_info.candidate_count; ++i) {
                const retrode3_candidate_v2 &c = probe_info.candidates[i];
                if (selected != c.id) continue;
                char hex[9];
                std::snprintf(hex, sizeof(hex), "%08X", c.payload_crc32);
                expected = retrode3::json_quote_v3(hex);
                break;
            }
        }
        const std::string mirroring =
            (old.header[6] & 8u) ? "four_screen" :
            (old.header[6] & 1u) ? "vertical" : "horizontal";
        std::string details = std::string("{\"read_configuration\":{") +
            "\"layout_id\":" + retrode3::json_quote_v3(old.layout_id) +
            ",\"mapper\":" + std::to_string(actual_mapper) +
            ",\"submapper\":" + std::to_string(actual_submapper) +
            ",\"prg_kib\":" + std::to_string(prg) +
            ",\"chr_kib\":" + std::to_string(chr) +
            ",\"mirroring\":" + retrode3::json_quote_v3(mirroring) +
            ",\"battery\":" + ((old.header[6] & 2u) ? "true" : "false") +
            "},\"identity\":{\"selected_candidate_id\":" +
                (selected.empty() ? "null" : retrode3::json_quote_v3(selected)) +
            ",\"matched_candidate_id\":" +
                (matched.empty() ? "null" : retrode3::json_quote_v3(matched)) +
            ",\"title\":" + retrode3::json_quote_v3(title) +
            "},\"verification\":{\"status\":" + retrode3::json_quote_v3(status) +
            ",\"verified\":" + (status == "verified" ? "true" : "false") +
            ",\"method\":\"payload_crc32\",\"expected\":" + expected +
            ",\"actual\":" + retrode3::json_quote_v3(crc) + "}}";
        if (details.size() >= sizeof(result->details_json))
            return fail(err, cap, "NES v3 result metadata too large");
        std::memcpy(result->details_json, details.c_str(), details.size() + 1);
        return 0;
    } catch (const std::exception &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown NES v3 read error"); }
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
        "nes", "Nintendo Entertainment System", "0.2.0-nrom-mmc1", "nes"
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

extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void)
{
    static const retrode3_core_operations_v3 operations = {
        RETRODE3_CORE_OPERATION_ABI_V3, sizeof(retrode3_core_operations_v3),
        capabilities_json_v3, fast_probe_json_v3, plan_read_v3, read_rom_v3
    };
    return &operations;
}
