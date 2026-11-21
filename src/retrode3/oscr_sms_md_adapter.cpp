// SMS-0007 passive Retrode2 adapter for the shared Retrode3 v3 host.
// Explicit experimental ROM-only path via kernel adapter lane mode 20.
// No mapper writes, SRAM or power switching. Header probe needs no opt-in.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v3.h"
#include "retrode3/sms_md_posix_v1.h"
#include "retrode3/sms_rom_core_v1.h"
#include "retrode3/json_v3.h"
#include "oscr_sms_catalog.h"
#include "oscr_sms_header_catalog.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const char *const kSchema = "sms-md-layout-v1";

int fail(char *err, size_t cap, const char *why)
{
    if (err && cap) std::snprintf(err, cap, "%s", why);
    return -1;
}
int copy_json(const std::string &json, char *out, size_t cap, char *err, size_t ec)
{
    if (!out || json.size() >= cap) return fail(err, ec, "SMS JSON buffer too small");
    std::memcpy(out, json.c_str(), json.size() + 1);
    return 0;
}
bool terminated(const char *s, size_t cap)
{
    return std::memchr(s, '\0', cap) != NULL;
}

// Track exactly the payload sent to the shared host; this CRC32 uses the
// standard reflected polynomial and identifies a full ROM only after reading.
struct SmsCrcSink {
    const retrode3_host_io_v1 *host;
    std::uint32_t raw_crc;
};
int stream_with_crc(void *context, const void *data, size_t size)
{
    SmsCrcSink *sink = static_cast<SmsCrcSink *>(context);
    if (sink->host->write(sink->host->context, data, size))
        return -1;
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i) {
        sink->raw_crc ^= bytes[i];
        for (unsigned b = 0; b < 8u; ++b)
            sink->raw_crc = (sink->raw_crc >> 1) ^
                ((sink->raw_crc & 1u) ? 0xedb88320u : 0u);
    }
    return 0;
}
int capabilities(char *out, size_t cap, char *err, size_t ec)
{
    static const char json[] = R"SMS({
"schema_version":1,
"schema_id":"sms-md-layout-v1",
"probe_priority":100,
"hardware_access":{"version":1,
 "probe":{"voltage_mv":3300,
          "module_parameters":{"legacy_bridge_hiz_test":true,"sms_mapper_write_test":false}},
 "read":{"voltage_mv":5000,
         "module_parameters":{"legacy_bridge_hiz_test":true,"sms_mapper_write_test":true},
         "environment":{"RETRODE3_SMS_MD_EXPERIMENT":"enable-readonly-v1",
                        "RETRODE3_SMS_MD_MAPPER_EXPERIMENT":"enable-sega16k-v1"}}},
"selection_modes":["auto","candidate","layout","override"],
"fields":[
  {"id":"mapping","label":"ROM mapping","type":"enum","required":true,
   "values":[{"value":"linear","label":"Linear (8-32 KiB)","read_supported":true},
             {"value":"sega16k","label":"Sega 16-KiB banks","read_supported":true}]},
  {"id":"rom_kib","label":"ROM size (KiB)","type":"enum","required":true,
   "values":[8,16,32,48,64,128,256,512,1024]}],
"rules":[{"when":{"mapping":"linear"},"allow":{"rom_kib":[8,16,32]},
          "read_supported":true,"experimental":true,
          "reason":"Requires explicit operator opt-in, 5V and SMS-0007 hardware acceptance"},
         {"when":{"mapping":"sega16k"},"read_supported":true,"experimental":true,
          "reason":"Requires explicit 5V mapper opt-in, gated kernel mode 22 and electrical acceptance"}],
"hardware_mode":"experimental-read-only","probe_voltage_mv":3300,
"read_voltage_mv":5000,"write_operations":false,
"mapper_register_writes":"experimental-opt-in-only"
})SMS";
    // Availability describes geometry support, not the current sysfs state.
    // The generic host applies the declared read profile before plan_read.
    return copy_json(json, out, cap, err, ec);
}

// Header geometry is a read-only suggestion. The core owns the actual
// mapping choice; the shared host never needs to know SMS ROM sizes.
retrode3::sms::Layout detected_layout(const retrode3::sms::Probe &probe)
{
    if (!probe.header_found || probe.conflicting_headers ||
        !probe.header.declared_rom_bytes ||
        probe.header.declared_rom_bytes % 1024u != 0u)
        throw std::runtime_error("SMS layout requires manual selection");
    const unsigned kib = probe.header.declared_rom_bytes / 1024u;
    retrode3::sms::Layout layout = {kib <= 32u ?
        retrode3::sms::Linear : retrode3::sms::Sega16k, kib};
    if (!retrode3::sms::valid_layout(layout))
        throw std::runtime_error("unsupported SMS auto layout");
    return layout;
}

// Bounded, read-only identification only. MAME product numbers are hints,
// NOT a verified game signature or a mapper authorization. Ignore unreliable
// or absent BCD codes and require the same declared physical ROM size.
std::string candidate_id(std::uint32_t crc)
{
    char id[20] = {};
    std::snprintf(id, sizeof(id), "sms-crc-%08X", crc);
    return id;
}
std::vector<const retrode3::sms::HeaderCandidate *> header_candidates(
    const retrode3::sms::Probe &p)
{
    std::vector<const retrode3::sms::HeaderCandidate *> result;
    if (!p.header_found || p.conflicting_headers ||
        !p.header.product_code_valid || !p.header.product_code)
        return result;
    for (std::size_t i = 0; i < retrode3::sms::kHeaderCandidateCount; ++i) {
        const retrode3::sms::HeaderCandidate &item =
            retrode3::sms::kHeaderCandidates[i];
        if (item.product_code == p.header.product_code &&
            item.size_bytes == p.header.declared_rom_bytes)
            result.push_back(&item);
    }
    return result;
}

// Different regional versions of the same game may share a product code.
// In that case the generic Web can show the common game name and offer its
// individual CRC-based candidates without falsely claiming a unique version.
std::string shared_game_name(
    const std::vector<const retrode3::sms::HeaderCandidate *> &matches)
{
    if (matches.empty()) return "";
    const std::string first(matches[0]->title);
    const std::string::size_type pos = first.find(" (");
    if (pos == std::string::npos) return matches.size() == 1 ? first : "";
    const std::string base = first.substr(0, pos);
    for (std::size_t i = 1; i < matches.size(); ++i) {
        const std::string name(matches[i]->title);
        if (name != base && name.compare(0, base.size() + 2, base + " (") != 0)
            return "";
    }
    return base;
}

retrode3::sms::Layout checked_layout(const retrode3_selection_v3 *s)
{
    if (!s || s->struct_size != sizeof(*s) || s->protocol_version != 1 ||
        !terminated(s->schema_id, sizeof(s->schema_id)) ||
        std::strcmp(s->schema_id, kSchema) != 0 ||
        !terminated(s->candidate_id, sizeof(s->candidate_id)) ||
        !terminated(s->layout_id, sizeof(s->layout_id)) ||
        s->parameter_count > RETRODE3_MAX_PARAMETERS_V3)
        throw std::runtime_error("invalid SMS selection ABI or schema");
    if (s->mode == RETRODE3_SELECT_CANDIDATE_V3) {
        if (!s->candidate_id[0] || s->layout_id[0] || s->parameter_count)
            throw std::runtime_error("invalid SMS candidate selection");
        retrode3::sms::SmsMdPosixBus probe_bus(NULL, NULL,
                                                retrode3::sms::SmsMdHeaderProbe);
        const retrode3::sms::Probe p = retrode3::sms::bounded_probe(probe_bus);
        const std::vector<const retrode3::sms::HeaderCandidate *> matches =
            header_candidates(p);
        for (std::size_t i = 0; i < matches.size(); ++i)
            if (candidate_id(matches[i]->full_crc32) == s->candidate_id)
                return detected_layout(p);
        throw std::runtime_error("SMS candidate no longer matches cartridge header");
    }
    if (s->candidate_id[0])
        throw std::runtime_error("SMS candidate ID only valid in candidate mode");
    retrode3::sms::Layout layout = {retrode3::sms::Linear, 0u};
    if (s->mode == RETRODE3_SELECT_AUTO_V3) {
        if (s->layout_id[0] || s->parameter_count)
            throw std::runtime_error("SMS auto selection contains overrides");
        // Bounded reads of three header windows, never mapper writes.
        // Recheck on BOTH plan and read; fail closed on conflicting data.
        retrode3::sms::SmsMdPosixBus probe_bus(NULL, NULL,
                                                retrode3::sms::SmsMdHeaderProbe);
        return detected_layout(retrode3::sms::bounded_probe(probe_bus));
    }
    if (s->mode == RETRODE3_SELECT_LAYOUT_V3) {
        if (s->parameter_count) throw std::runtime_error("SMS layout has parameters");
        if (std::strcmp(s->layout_id, "linear-8") == 0) layout.rom_kib = 8;
        else if (std::strcmp(s->layout_id, "linear-16") == 0) layout.rom_kib = 16;
        else if (std::strcmp(s->layout_id, "linear-32") == 0) layout.rom_kib = 32;
        else if (std::strcmp(s->layout_id, "sega16k-48") == 0)
            layout = retrode3::sms::Layout{retrode3::sms::Sega16k, 48};
        else if (std::strcmp(s->layout_id, "sega16k-64") == 0)
            layout = retrode3::sms::Layout{retrode3::sms::Sega16k, 64};
        else if (std::strcmp(s->layout_id, "sega16k-128") == 0)
            layout = retrode3::sms::Layout{retrode3::sms::Sega16k, 128};
        else if (std::strcmp(s->layout_id, "sega16k-256") == 0)
            layout = retrode3::sms::Layout{retrode3::sms::Sega16k, 256};
        else if (std::strcmp(s->layout_id, "sega16k-512") == 0)
            layout = retrode3::sms::Layout{retrode3::sms::Sega16k, 512};
        else if (std::strcmp(s->layout_id, "sega16k-1024") == 0)
            layout = retrode3::sms::Layout{retrode3::sms::Sega16k, 1024};
        else throw std::runtime_error("unknown or unsupported SMS layout");
    } else if (s->mode == RETRODE3_SELECT_OVERRIDE_V3) {
        if (s->layout_id[0] || s->parameter_count != 2)
            throw std::runtime_error("SMS override requires mapping and rom_kib");
        bool has_mapping = false, has_size = false;
        for (unsigned i = 0; i < s->parameter_count; ++i) {
            const retrode3_parameter_v3 &p = s->parameters[i];
            if (!terminated(p.id, sizeof(p.id)))
                throw std::runtime_error("invalid SMS parameter name");
            if (std::strcmp(p.id, "mapping") == 0) {
                if (has_mapping || p.type != RETRODE3_PARAMETER_STRING_V3 ||
                    !terminated(p.string_value, sizeof(p.string_value)))
                    throw std::runtime_error("invalid SMS mapping");
                if (std::strcmp(p.string_value, "linear") == 0)
                    layout.mapping = retrode3::sms::Linear;
                else if (std::strcmp(p.string_value, "sega16k") == 0)
                    layout.mapping = retrode3::sms::Sega16k;
                else throw std::runtime_error("unknown SMS mapping");
                has_mapping = true;
            } else if (std::strcmp(p.id, "rom_kib") == 0) {
                if (has_size || p.type != RETRODE3_PARAMETER_INTEGER_V3)
                    throw std::runtime_error("invalid SMS size parameter");
                layout.rom_kib = p.integer_value;
                has_size = true;
            } else throw std::runtime_error("unknown SMS override parameter");
        }
        if (!has_mapping || !has_size)
            throw std::runtime_error("missing SMS layout fields");
    } else throw std::runtime_error("unsupported SMS selection mode");
    if (!retrode3::sms::valid_layout(layout))
        throw std::runtime_error("unsupported SMS mapping/size combination");
    return layout;
}

int fast_probe(char *out, size_t cap, char *err, size_t ec)
{
    // The initial system-identification probe is read-only at 3.3 V.
    // Full ROM dumping still requires explicit opt-in and confirmed 5 V.
    if (!retrode3::sms::sms_voltage_allows_probe())
        return copy_json(R"SMS({"identification_status":"unknown", "ambiguity":"none",
"candidate_lookup":"hardware_unavailable", "selection_required":true,
"candidates":[],"read_layouts":[],"adapter_status":"probe_voltage_unavailable",
"read_supported":false,"system_detected":false})SMS", out, cap, err, ec);
    try {
        retrode3::sms::SmsMdPosixBus bus(NULL, NULL, retrode3::sms::SmsMdHeaderProbe);
        const retrode3::sms::Probe p = retrode3::sms::bounded_probe(bus);
        std::string layouts = "[]";
        std::string layout_id;
        bool auto_layout = false;
        if (p.header_found && !p.conflicting_headers) {
            const unsigned kib = p.header.declared_rom_bytes / 1024u;
            if (kib == 8u || kib == 16u || kib == 32u) {
                auto_layout = true;
                layout_id = "linear-" + std::to_string(kib);
                layouts = "[{\"id\":\"" + layout_id +
                    "\",\"label\":\"SMS linear " + std::to_string(kib) +
                    " KiB\",\"read_supported\":true}]";
            } else if (kib == 48u || kib == 64u || kib == 128u ||
                       kib == 256u || kib == 512u || kib == 1024u) {
                // Probe is deliberately at 3.3V without mapper writes;
                // read_support means the layout can be read after the
                // generic host's scoped read profile has been applied.
                auto_layout = true;
                layout_id = "sega16k-" + std::to_string(kib);
                layouts = "[{\"id\":\"" + layout_id +
                    "\",\"label\":\"SMS Sega 16-KiB banks " +
                    std::to_string(kib) + " KiB\",\"read_supported\":true,"
                    "\"experimental\":true}]";
            }
        }
        const std::vector<const retrode3::sms::HeaderCandidate *> matches =
            auto_layout ? header_candidates(p) :
                std::vector<const retrode3::sms::HeaderCandidate *>();
        const std::string status = matches.empty() ? "unknown" :
            matches.size() == 1 ? "unique" : "ambiguous";
        const std::string ambiguity = matches.size() > 1 ?
            "titles_only" : "none";
        const std::string common = shared_game_name(matches);
        std::string candidates = "[";
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (i) candidates += ",";
            candidates += "{\"id\":" +
                retrode3::json_quote_v3(candidate_id(matches[i]->full_crc32)) +
                ",\"title\":" + retrode3::json_quote_v3(matches[i]->title) +
                ",\"layout_id\":" + retrode3::json_quote_v3(layout_id) +
                ",\"read_supported\":true}";
        }
        candidates += "]";
        const std::string json = "{\"identification_status\":" +
            retrode3::json_quote_v3(status) +
            ",\"ambiguity\":" + retrode3::json_quote_v3(ambiguity) +
            ",\"candidate_lookup\":\"" +
            std::string(matches.empty() ? "header_only" : "header_product_code") +
            "\",\"title\":" +
            (common.empty() ? "null" : retrode3::json_quote_v3(common)) +
            ",\"selection_required\":" + std::string(auto_layout ? "false" : "true") +
            ",\"candidates\":" + candidates + ",\"read_layouts\":" +
            layouts + ",\"probe_fingerprints\":{\"product_code\":" +
            std::string(p.header_found && p.header.product_code_valid ?
                std::to_string(p.header.product_code) : "null") +
            ",\"revision\":" + std::to_string(p.header_found ? p.header.revision : 0u) +
            ",\"header_checksum\":" +
            std::to_string(p.header_found ? p.header.checksum : 0u) +
            "},\"adapter_status\":\"experimental\","
            "\"system_detected\":" +
            ((p.header_found && !p.conflicting_headers) ? "true" : "false") +
            ",\"header_found\":" + (p.header_found ? "true" : "false") +
            ",\"conflicting_headers\":" + (p.conflicting_headers ? "true" : "false") +
            ",\"header_rom_bytes\":" +
            std::to_string(p.header_found ? p.header.declared_rom_bytes : 0u) + "}";
        return copy_json(json, out, cap, err, ec);
    } catch (const std::exception &e) { return fail(err, ec, e.what()); }
    catch (...) { return fail(err, ec, "SMS header probe failed"); }
}

int plan(const retrode3_selection_v3 *s, retrode3_read_plan_v3 *p,
         char *err, size_t cap)
{
    if (!p || p->struct_size != sizeof(*p))
        return fail(err, cap, "invalid SMS read-plan ABI");
    try {
        const retrode3::sms::Layout layout = checked_layout(s);
        if (!retrode3::sms::sms_experiment_enabled())
            return fail(err, cap, "SMS experimental read requires explicit opt-in");
        if (layout.mapping == retrode3::sms::Sega16k &&
            (!retrode3::sms::sms_mapper_experiment_enabled() ||
             !retrode3::sms::sms_kernel_mapper_gate_enabled()))
            return fail(err, cap, "SMS mapper needs explicit opt-in and 7.3 kernel mode 22");
        if (!retrode3::sms::sms_voltage_is_5v())
            return fail(err, cap, "SMS requires confirmed 5000mV on MD slot");
        p->prefix_bytes = 0;
        p->payload_bytes = layout.rom_kib * 1024u;
        return 0;
    } catch (const std::exception &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "SMS planning failed"); }
}

int read_rom(const retrode3_selection_v3 *s, const retrode3_host_io_v1 *io,
             retrode3_read_result_v3 *result, char *err, size_t cap)
{
    if (!io || io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
        io->struct_size < sizeof(*io) || !io->write || !io->cancelled ||
        !result || result->struct_size != sizeof(*result))
        return fail(err, cap, "invalid SMS host IO/result ABI");
    try {
        const retrode3::sms::Layout layout = checked_layout(s);
        if (layout.mapping == retrode3::sms::Sega16k &&
            (!retrode3::sms::sms_mapper_experiment_enabled() ||
             !retrode3::sms::sms_kernel_mapper_gate_enabled()))
            return fail(err, cap, "SMS mapper needs explicit opt-in and 7.3 kernel mode 22");
        retrode3::sms::SmsMdPosixBus bus(io->cancelled, io->context);
        if (!bus.approved())
            return fail(err, cap, "SMS read profile and 5000mV required");
        SmsCrcSink sink = {io, 0xffffffffu};
        retrode3::sms::stream_rom(bus, layout, &sink, stream_with_crc);
        const std::uint32_t crc = ~sink.raw_crc;
        result->prefix_bytes = 0;
        result->payload_crc_known = 1;
        result->payload_crc32 = crc;
        const retrode3::sms::CatalogEntry *entry = retrode3::sms::catalog_lookup(crc);
        char crc_text[9] = {};
        std::snprintf(crc_text, sizeof(crc_text), "%08X", crc);
        const std::string crc_json = retrode3::json_quote_v3(crc_text);
        const std::string match_id = candidate_id(crc);
        const std::string selected_id =
            s->mode == RETRODE3_SELECT_CANDIDATE_V3 ? s->candidate_id : "";
        const bool mismatch = !selected_id.empty() &&
            (!entry || selected_id != match_id);
        const std::string verified_status = mismatch ? "mismatch" :
            entry ? "verified" : "not_found";
        const std::string details = "{\"read_configuration\":{\"mapping\":\"" +
            std::string(layout.mapping == retrode3::sms::Sega16k ?
                        "sega16k" : "linear") + "\","
            "\"rom_kib\":" + std::to_string(layout.rom_kib) +
            "},\"identity\":{\"selected_candidate_id\":" +
            (selected_id.empty() ? "null" : retrode3::json_quote_v3(selected_id)) +
            ",\"matched_candidate_id\":" +
            (entry ? retrode3::json_quote_v3(match_id) : "null") +
            ",\"title\":" + (entry ? retrode3::json_quote_v3(entry->title) : "null") +
            "},\"verification\":{\"status\":\"" +
            verified_status +
            "\",\"verified\":" + (verified_status == "verified" ? "true" : "false") +
            ",\"method\":\"full_rom_crc32\",\"expected\":" +
            (entry ? crc_json : "null") + ",\"actual\":" + crc_json + "}}";
        if (details.size() >= sizeof(result->details_json))
            return fail(err, cap, "SMS result metadata too long");
        std::memcpy(result->details_json, details.c_str(), details.size() + 1);
        return 0;
    } catch (const std::exception &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "SMS ROM read failed"); }
}

int v1_probe(retrode3_probe_v1 *p, char *err, size_t ec)
{
    if (!p || p->struct_size != sizeof(*p))
        return fail(err, ec, "invalid SMS v1 probe ABI");
    return fail(err, ec, "SMS requires the common CLI v3 selection API");
}
void release() {}
} // namespace

extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void)
{
    static const retrode3_core_descriptor_v1 d = {
        RETRODE3_CORE_DISCOVERY_ABI_V1, sizeof(d),
        "sms-md", "Master System (SMS-0007 adapter)", "0.4.0-oscr-generated-sega16k-lab", "md"
    };
    return &d;
}
extern "C" const retrode3_core_operations_v1 *retrode3_core_get_operations_v1(void)
{
    static const retrode3_core_operations_v1 ops = {
        RETRODE3_CORE_OPERATION_ABI_V1, sizeof(ops), v1_probe, NULL, release
    };
    return &ops;
}
extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void)
{
    static const retrode3_core_operations_v3 ops = {
        RETRODE3_CORE_OPERATION_ABI_V3, sizeof(ops),
        capabilities, fast_probe, plan, read_rom
    };
    return &ops;
}