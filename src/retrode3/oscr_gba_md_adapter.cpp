// Retrode2 GBx-0011 on the MegaDrive slot: GBA ROM-only v3 core.
// The common CLI's scoped hardware_access sets 3.3 V and gbx_md_test
// before each probe/plan/read. The first kernel GBx pread prepares
// safe read-only bus directions; no Python arm helper is required.
// No cartridge writes or unscoped voltage changes.
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v3.h"
#include "retrode3/json_v3.h"
#include "gbx/retrode2_readonly.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/types.h>
#include <unistd.h>

namespace {
namespace gbx = OSCR::Cores::Retrode2GBx;
const char *const SCHEMA = "gba-md-size-v1";
const unsigned MIB = 1024u * 1024u;
const unsigned MODE_GBA_SLOW = 24u;
const unsigned MODE_GBA_BURST = 28u;

int fail(char *error, std::size_t cap, const char *message)
{
    if (error && cap) std::snprintf(error, cap, "%s", message);
    return -1;
}
int copy_json(const std::string &doc, char *out, std::size_t capacity,
              char *error, std::size_t error_capacity)
{
    if (!out || doc.size() >= capacity)
        return fail(error, error_capacity, "GBA JSON buffer too small");
    std::memcpy(out, doc.c_str(), doc.size() + 1);
    return 0;
}
bool nul_terminated(const char *s, std::size_t capacity)
{
    return std::memchr(s, '\0', capacity) != NULL;
}
std::string read_line(const char *name)
{
    int fd = ::open(name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error(std::string("cannot read GBx hardware state: ") + name);
    char data[64];
    const ssize_t size = ::read(fd, data, sizeof(data) - 1);
    const int saved_errno = errno;
    ::close(fd);
    if (size <= 0) {
        (void)saved_errno;
        throw std::runtime_error("empty GBx hardware state");
    }
    data[size] = '\0';
    return std::string(data, std::strcspn(data, "\r\n"));
}
const char *device_path()
{
#ifdef RETRODE3_TESTING
    const char *path = std::getenv("RETRODE3_GBA_TEST_DEVICE");
    if (path && *path) return path;
#endif
    return "/dev/slot-md";
}
std::string state_root()
{
#ifdef RETRODE3_TESTING
    const char *root = std::getenv("RETRODE3_GBA_TEST_STATE_ROOT");
    if (root && *root) return root;
#endif
    return "/sys";
}
void check_bench_ready()
{
    const std::string root = state_root();
    const std::string param = read_line((root + "/module/retrode3/parameters/gbx_md_test").c_str());
    if (param != "Y" && param != "1")
        throw std::runtime_error("GBx read-only probe profile not enabled (gbx_md_test); hardware scope required");
    const std::string vcc = read_line((root + "/class/retrode3/slot-md/vcc").c_str());
    if (vcc != "3300mV")
        throw std::runtime_error("GBA requires exactly 3300mV; refusing bus access");
    // The first mode-24 kernel pread in read_header() performs the
    // GBx pin-direction preparation before any GBA /RD strobe.
    // The kernel must also support safely restoring the shared MD bus
    // when the scoped probe ends. See GBx integration documentation.
}
class RomDevice {
    int fd_;
    RomDevice(const RomDevice &);
    RomDevice &operator=(const RomDevice &);
public:
    RomDevice() : fd_(-1) {
        check_bench_ready();
        fd_ = ::open(device_path(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd_ < 0) throw std::runtime_error("cannot open /dev/slot-md (GBx bench not armed?)");
    }
    ~RomDevice() { if (fd_ >= 0) ::close(fd_); }
    void read_mode(unsigned mode, unsigned address, void *bytes, std::size_t size) const {
        if (!size || (mode != MODE_GBA_SLOW && mode != MODE_GBA_BURST) ||
            static_cast<std::uint64_t>(address) + size > 0x1000000ull)
            throw std::runtime_error("GBA read exceeds validated low ROM window");
        const std::uint64_t offset =
            (static_cast<std::uint64_t>(mode) << 24) | address;
        const ssize_t got = ::pread(fd_, bytes, size, static_cast<off_t>(offset));
        if (got != static_cast<ssize_t>(size))
            throw std::runtime_error("short GBA ROM read or kernel transport error");
    }
};
#include "oscr_gba_generated.h"
#include "oscr_gba_catalog.h"

void copy_printable(char *out, const unsigned char *source, std::size_t count)
{
    std::size_t i = 0;
    while (i < count && source[i]) {
        const unsigned char c = source[i];
        out[i++] = (c >= 0x20 && c <= 0x7e) ? static_cast<char>(c) : '_';
    }
    while (i && out[i - 1] == ' ') --i;
    out[i] = 0;
}
gbx::GbaHeader read_header(RomDevice &device)
{
    unsigned char bytes[192] = {};
    oscr_gba_header(device, bytes);
    gbx::GbaHeader header = {{0}, {0}, 0};
    copy_printable(header.title, bytes + 0xa0, 12);
    copy_printable(header.game_code, bytes + 0xac, 4);
    header.header_checksum = bytes[0xbd];
    return header;
}
std::string title_of(const gbx::GbaHeader &header)
{
    return header.title[0] ? header.title : "Game Boy Advance cartridge";
}
std::string layout(unsigned size_mib)
{
    return std::string("gba-") + std::to_string(size_mib) + "m";
}
bool supported_size(unsigned n) { return n == 1 || n == 2 || n == 4 || n == 8 || n == 16; }
std::string crc_text(uint32_t crc) {
    char s[9]; std::snprintf(s, sizeof(s), "%08X", crc); return s;
}
std::string candidate_id(const GbaRecord &r) { return "gba-crc-" + crc_text(r.crc); }
std::vector<const GbaRecord *> matches(const gbx::GbaHeader &h) {
    std::vector<const GbaRecord *> out;
    for (const auto &r : gba_catalog)
        if (std::strcmp(r.serial, h.game_code) == 0) out.push_back(&r);
    return out;
}
unsigned common_size(const std::vector<const GbaRecord *> &m) {
    if (m.empty() || !supported_size(m[0]->mib)) return 0;
    for (const auto *r : m) if (r->mib != m[0]->mib) return 0;
    return m[0]->mib;
}
unsigned select_size(const retrode3_selection_v3 *selected, const gbx::GbaHeader &h)
{
    if (!selected || selected->struct_size != sizeof(*selected) ||
        selected->protocol_version != 1u ||
        !nul_terminated(selected->schema_id, sizeof(selected->schema_id)) ||
        std::strcmp(selected->schema_id, SCHEMA) != 0 ||
        !nul_terminated(selected->candidate_id, sizeof(selected->candidate_id)) ||
        !nul_terminated(selected->layout_id, sizeof(selected->layout_id)) ||
        selected->parameter_count > RETRODE3_MAX_PARAMETERS_V3)
        throw std::runtime_error("invalid or stale GBA selection");
    unsigned value = 0;
    if (selected->mode == RETRODE3_SELECT_AUTO_V3) {
        if (selected->candidate_id[0] || selected->layout_id[0] || selected->parameter_count)
            throw std::runtime_error("invalid auto GBA selection");
        value = common_size(matches(h));
    } else if (selected->mode == RETRODE3_SELECT_CANDIDATE_V3) {
        if (!selected->candidate_id[0] || selected->layout_id[0] || selected->parameter_count)
            throw std::runtime_error("invalid GBA candidate selection");
        for (const auto *r : matches(h))
            if (candidate_id(*r) == selected->candidate_id) value = r->mib;
    } else if (selected->mode == RETRODE3_SELECT_LAYOUT_V3) {
        if (selected->candidate_id[0]) throw std::runtime_error("unexpected GBA candidate");
        if (selected->parameter_count) throw std::runtime_error("layout cannot include parameters");
        for (unsigned i = 1; i <= 16; i *= 2)
            if (std::strcmp(selected->layout_id, layout(i).c_str()) == 0)
                value = i;
    } else if (selected->mode == RETRODE3_SELECT_OVERRIDE_V3) {
        if (selected->candidate_id[0] || selected->layout_id[0] || selected->parameter_count != 1)
            throw std::runtime_error("GBA override requires rom_mib only");
        const retrode3_parameter_v3 &p = selected->parameters[0];
        if (!nul_terminated(p.id, sizeof(p.id)) ||
            std::strcmp(p.id, "rom_mib") ||
            p.type != RETRODE3_PARAMETER_INTEGER_V3)
            throw std::runtime_error("unknown or invalid GBA override field");
        value = p.integer_value;
    } else {
        throw std::runtime_error("GBA ROM size is not in header; select a layout explicitly");
    }
    if (value != 1 && value != 2 && value != 4 && value != 8 && value != 16)
        throw std::runtime_error("unverified GBA ROM size: 1/2/4/8/16 MiB only");
    return value;
}

int capabilities(char *out, std::size_t cap, char *err, std::size_t ec)
{
    const std::string schema =
        "{\"schema_version\":1,\"schema_id\":\"gba-md-size-v1\","
        "\"selection_modes\":[\"auto\",\"candidate\",\"layout\",\"override\"],"
        "\"fields\":[{\"id\":\"rom_mib\",\"label\":\"ROM size (MiB)\","
        "\"type\":\"enum\",\"required\":true,\"values\":["
        "{\"value\":1,\"label\":\"1 MiB\"},"
        "{\"value\":2,\"label\":\"2 MiB\"},"
        "{\"value\":4,\"label\":\"4 MiB\"},"
        "{\"value\":8,\"label\":\"8 MiB\"},"
        "{\"value\":16,\"label\":\"16 MiB\"},"
        "{\"value\":32,\"label\":\"32 MiB\",\"read_supported\":false}]}],"
        "\"rules\":[{\"when\":{\"rom_mib\":32},\"read_supported\":false,"
        "\"reason\":\"upper 16 MiB ROM window not hardware verified\"}],"
        "\"probe_priority\":150,\"probe_without_sense\":true,\"output_system\":\"gba\","
        "\"hardware_access\":{\"version\":1,"
        "\"probe\":{\"voltage_mv\":3300,"
        "\"module_parameters\":{\"gbx_md_test\":true,"
        "\"legacy_bridge_hiz_test\":false,\"sms_mapper_write_test\":false}},"
        "\"read\":{\"voltage_mv\":3300,"
        "\"module_parameters\":{\"gbx_md_test\":true,"
        "\"legacy_bridge_hiz_test\":false,\"sms_mapper_write_test\":false}}},"
        "\"probe_voltage_mv\":3300,\"read_voltage_mv\":3300,"
        "\"automatic_voltage_switch\":true,"
        "\"bench_only\":true,\"requires_manual_empty_slot_arm\":false,"
        "\"write_operations\":false}"
        ;
    return copy_json(schema, out, cap, err, ec);
}
int probe_json(char *out, std::size_t cap, char *err, std::size_t ec)
{
    try {
        RomDevice device;
        const gbx::GbaHeader header = read_header(device);
        const auto found = matches(header);
        const unsigned size = common_size(found);
        std::string candidates = "[", layouts = "[";
        for (const auto *r : found) {
            if (candidates.size() > 1) candidates += ",";
            candidates += "{\"id\":" + retrode3::json_quote_v3(candidate_id(*r)) +
                ",\"title\":" + retrode3::json_quote_v3(r->title) +
                ",\"layout_id\":" + retrode3::json_quote_v3(layout(r->mib)) +
                ",\"read_supported\":" + (supported_size(r->mib) ? "true" : "false") + "}";
        }
        candidates += "]";
        for (unsigned n = 1; n <= 32; n *= 2) {
            if (!found.empty()) {
                bool listed = false;
                for (const auto *r : found) if (r->mib == n) listed = true;
                if (!listed) continue;
            } else if (!supported_size(n)) continue;
            if (layouts.size() > 1) layouts += ",";
            layouts += "{\"id\":" + retrode3::json_quote_v3(layout(n)) +
                ",\"label\":\"GBA " + std::to_string(n) +
                " MiB\",\"read_supported\":" + (supported_size(n) ? "true" : "false") +
                ",\"size_bytes\":" + std::to_string(n * MIB) + "}";
        }
        layouts += "]";
        const std::string status = found.empty() ? "unknown" : found.size() == 1 ? "unique" : "ambiguous";
        const std::string ambiguity = !size ? "layouts" : found.size() > 1 ? "titles_only" : "none";
        const std::string data = "{\"identification_status\":" + retrode3::json_quote_v3(status) +
            ",\"ambiguity\":" + retrode3::json_quote_v3(ambiguity) +
            ",\"selection_required\":" + (size ? "false" : "true") +
            ",\"candidate_lookup\":\"header_serial\",\"system_detected\":true,\"candidates\":" +
            candidates + ",\"read_layouts\":" + layouts +
            ",\"size_bytes\":" + std::to_string(size * MIB) + ",\"mapping\":\"linear\",\"title\":" +
            retrode3::json_quote_v3(found.size() == 1 ? found[0]->title : title_of(header)) +
            ",\"game_code\":" + retrode3::json_quote_v3(header.game_code) +
            ",\"save\":{\"read_supported\":false,\"write_supported\":false}}";
        return copy_json(data, out, cap, err, ec);
    } catch (std::exception const &e) { return fail(err, ec, e.what()); }
    catch (...) { return fail(err, ec, "unknown GBA probe error"); }
}
int v1_probe(retrode3_probe_v1 *result, char *err, std::size_t cap)
{
    try {
        if (!result || result->struct_size != sizeof(*result))
            return fail(err, cap, "invalid GBA v1 probe ABI");
        RomDevice device;
        const gbx::GbaHeader header = read_header(device);
        result->rom_size_bytes = 0; // The header does NOT provide ROM geometry.
        std::snprintf(result->title, sizeof(result->title), "%s", title_of(header).c_str());
        std::snprintf(result->mapping, sizeof(result->mapping), "%s", "unknown");
        result->checksum[0] = '\0';
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown GBA v1 probe error"); }
}
int plan(const retrode3_selection_v3 *sel, retrode3_read_plan_v3 *result,
         char *err, std::size_t cap)
{
    try {
        if (!result || result->struct_size != sizeof(*result))
            return fail(err, cap, "invalid GBA v3 plan ABI");
        RomDevice device;
        const unsigned mib = select_size(sel, read_header(device));
        result->payload_bytes = static_cast<std::uint64_t>(mib) * MIB;
        result->prefix_bytes = 0;
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown GBA plan error"); }
}
int read_rom(const retrode3_selection_v3 *sel, const retrode3_host_io_v1 *io,
             retrode3_read_result_v3 *result, char *err, std::size_t cap)
{
    try {
        if (!io || io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
            io->struct_size < sizeof(*io) || !io->write || !io->cancelled ||
            !result || result->struct_size != sizeof(*result))
            return fail(err, cap, "invalid GBA v3 host IO / result ABI");
        if (io->cancelled(io->context)) return fail(err, cap, "cancelled");
        RomDevice device;
        const gbx::GbaHeader before = read_header(device);
        const unsigned mib = select_size(sel, before);
        const std::uint32_t limit = mib * MIB;
        struct CrcSink {
            const retrode3_host_io_v1 *host;
            uint32_t crc;
            static int write(void *ctx, const void *p, size_t n) {
                CrcSink *s = static_cast<CrcSink *>(ctx);
                const unsigned char *b = static_cast<const unsigned char *>(p);
                for (size_t i = 0; i < n; ++i) {
                    s->crc ^= b[i];
                    for (unsigned k = 0; k < 8; ++k)
                        s->crc = (s->crc >> 1) ^ ((s->crc & 1u) ? 0xedb88320u : 0u);
                }
                return s->host->write(s->host->context, p, n);
            }
            static int cancel(void *ctx) {
                CrcSink *s = static_cast<CrcSink *>(ctx);
                return s->host->cancelled(s->host->context);
            }
        } sink = {io, 0xffffffffu};
        const retrode3_host_io_v1 wrapped = {io->abi_version, sizeof(wrapped), &sink,
                                              CrcSink::write, CrcSink::cancel};
        oscr_gba_stream(device, limit, &wrapped);
        const uint32_t crc = ~sink.crc;
        const GbaRecord *matched = NULL;
        unsigned hits = 0;
        for (const auto &r : gba_catalog)
            if (r.crc == crc && r.mib == mib) { matched = &r; ++hits; }
        const std::string selected = sel->mode == RETRODE3_SELECT_CANDIDATE_V3 ? sel->candidate_id : "";
        const bool verified = hits == 1 && (selected.empty() || selected == candidate_id(*matched));
        const std::string status = verified ? "verified" : !selected.empty() ? "mismatch" : hits > 1 ? "ambiguous" : "not_found";
        const gbx::GbaHeader after = read_header(device);
        if (std::strcmp(before.title, after.title) != 0 ||
            std::strcmp(before.game_code, after.game_code) != 0 ||
            before.header_checksum != after.header_checksum)
            return fail(err, cap, "GBA cartridge header changed during dump");
        const std::string details =
            std::string("{\"read_configuration\":{\"layout_id\":") +
            retrode3::json_quote_v3(layout(mib)) +
            ",\"rom_mib\":" + std::to_string(mib) +
            ",\"rom_size_bytes\":" + std::to_string(limit) +
            ",\"transport\":\"gbx-0011-burst\",\"voltage_mv\":3300},"
            "\"identity\":{\"selected_candidate_id\":" +
            (selected.empty() ? "null" : retrode3::json_quote_v3(selected)) +
            ",\"matched_candidate_id\":" + (hits == 1 ? retrode3::json_quote_v3(candidate_id(*matched)) : "null") +
            ",\"title\":" + retrode3::json_quote_v3(hits == 1 ? matched->title : title_of(before)) +
            ",\"game_code\":" + retrode3::json_quote_v3(before.game_code) +
            "},\"verification\":{\"status\":" + retrode3::json_quote_v3(status) +
            ",\"verified\":" + (verified ? "true" : "false") +
            ",\"method\":\"full_rom_crc32\",\"expected\":" +
            (!selected.empty() ? retrode3::json_quote_v3(selected.substr(8)) :
             verified ? retrode3::json_quote_v3(crc_text(crc)) : "null") +
            ",\"actual\":" + retrode3::json_quote_v3(crc_text(crc)) + "}}";
        if (details.size() >= sizeof(result->details_json))
            return fail(err, cap, "GBA v3 result details too long");
        result->prefix_bytes = 0;
        result->payload_crc_known = 1;
        result->payload_crc32 = crc;
        std::memcpy(result->details_json, details.c_str(), details.size() + 1);
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown GBA read error"); }
}
void release() {} // All fd ownership stays within each bus-locked callback.
} // namespace

extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void)
{
    static const retrode3_core_descriptor_v1 descriptor = {
        RETRODE3_CORE_DISCOVERY_ABI_V1, sizeof(retrode3_core_descriptor_v1),
        "gba-md", "Game Boy Advance (Retrode2 GBx-0011)", "0.1.0-bench", "md"
    };
    return &descriptor;
}
extern "C" const retrode3_core_operations_v1 *retrode3_core_get_operations_v1(void)
{
    static const retrode3_core_operations_v1 ops = {
        RETRODE3_CORE_OPERATION_ABI_V1, sizeof(retrode3_core_operations_v1),
        v1_probe, NULL, release
    };
    return &ops;
}
extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void)
{
    static const retrode3_core_operations_v3 ops = {
        RETRODE3_CORE_OPERATION_ABI_V3, sizeof(retrode3_core_operations_v3),
        capabilities, probe_json, plan, read_rom
    };
    return &ops;
}
