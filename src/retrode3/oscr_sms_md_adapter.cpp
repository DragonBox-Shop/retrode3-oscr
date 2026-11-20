// SMS-0007 passive Retrode2 adapter for the shared Retrode3 v3 host.
// Explicit experimental ROM-only path via kernel adapter lane mode 20.
// No mapper writes, no SRAM, no power switching, no implicit adapter detection.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v3.h"
#include "retrode3/sms_md_posix_v1.h"
#include "retrode3/sms_rom_core_v1.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

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
int capabilities(char *out, size_t cap, char *err, size_t ec)
{
    static const char json[] = R"SMS({
"schema_version":1,
"schema_id":"sms-md-layout-v1",
"selection_modes":["auto","layout","override"],
"fields":[
  {"id":"mapping","label":"ROM mapping","type":"enum","required":true,
   "values":[{"value":"linear","label":"Linear (8-32 KiB)","read_supported":true},
             {"value":"sega16k","label":"Sega 16-KiB banks","read_supported":false}]},
  {"id":"rom_kib","label":"ROM size (KiB)","type":"enum","required":true,
   "values":[8,16,32,48,64,128,256,512,1024]}],
"rules":[{"when":{"mapping":"linear"},"allow":{"rom_kib":[8,16,32]},
          "read_supported":true,"experimental":true,
          "reason":"Requires explicit operator opt-in, 5V and SMS-0007 hardware acceptance"},
         {"when":{"mapping":"sega16k"},"read_supported":false,
          "reason":"SMS mapper-register write cycles are disabled"}],
"hardware_mode":"experimental-read-only","write_operations":false
})SMS";
    return copy_json(json, out, cap, err, ec);
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
    if (s->candidate_id[0])
        throw std::runtime_error("SMS catalogue candidates are not supported");
    retrode3::sms::Layout layout = {retrode3::sms::Linear, 0u};
    if (s->mode == RETRODE3_SELECT_AUTO_V3) {
        if (s->layout_id[0] || s->parameter_count)
            throw std::runtime_error("SMS auto selection contains overrides");
        throw std::runtime_error("explicit SMS linear layout selection required");
    }
    if (s->mode == RETRODE3_SELECT_LAYOUT_V3) {
        if (s->parameter_count) throw std::runtime_error("SMS layout has parameters");
        if (std::strcmp(s->layout_id, "linear-8") == 0) layout.rom_kib = 8;
        else if (std::strcmp(s->layout_id, "linear-16") == 0) layout.rom_kib = 16;
        else if (std::strcmp(s->layout_id, "linear-32") == 0) layout.rom_kib = 32;
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
    if (layout.mapping != retrode3::sms::Linear)
        throw std::runtime_error("SMS bank-switch writes are disabled");
    return layout;
}

int fast_probe(char *out, size_t cap, char *err, size_t ec)
{
    if (!retrode3::sms::sms_experiment_enabled())
        return copy_json(R"SMS({"identification_status":"unknown", "ambiguity":"none",
"candidate_lookup":"hardware_disabled", "selection_required":true,
"candidates":[],"read_layouts":[],"adapter_status":"explicit_opt_in_required",
"read_supported":false})SMS", out, cap, err, ec);
    if (!retrode3::sms::sms_voltage_is_5v())
        return copy_json(R"SMS({"identification_status":"unknown", "ambiguity":"none",
"candidate_lookup":"hardware_unavailable", "selection_required":true,
"candidates":[],"read_layouts":[],"adapter_status":"5v_required",
"read_supported":false})SMS", out, cap, err, ec);
    try {
        retrode3::sms::SmsMdPosixBus bus;
        const retrode3::sms::Probe p = retrode3::sms::bounded_probe(bus);
        std::string layouts = "[]";
        if (p.header_found && !p.conflicting_headers) {
            const unsigned kib = p.header.declared_rom_bytes / 1024u;
            if (kib == 8u || kib == 16u || kib == 32u)
                layouts = "[{\"id\":\"linear-" + std::to_string(kib) +
                    "\",\"label\":\"SMS linear " + std::to_string(kib) +
                    " KiB\",\"read_supported\":true}]";
        }
        const std::string json = "{\"identification_status\":\"unknown\","
            "\"ambiguity\":\"none\",\"candidate_lookup\":\"header_only\","
            "\"selection_required\":true,\"candidates\":[],\"read_layouts\":" +
            layouts + ",\"adapter_status\":\"experimental\","
            "\"header_found\":" + (p.header_found ? "true" : "false") +
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
        retrode3::sms::SmsMdPosixBus bus(io->cancelled, io->context);
        if (!bus.approved())
            return fail(err, cap, "SMS opt-in and 5000mV required");
        retrode3::sms::stream_rom(bus, layout, io->context, io->write);
        result->prefix_bytes = 0;
        result->payload_crc_known = 0;
        const std::string details = "{\"read_configuration\":{\"mapping\":\"linear\","
            "\"rom_kib\":" + std::to_string(layout.rom_kib) +
            "},\"identity\":{\"selected_candidate_id\":null,"
            "\"matched_candidate_id\":null,\"title\":null},"
            "\"verification\":{\"status\":\"not_checked\",\"verified\":false}}";
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
        "sms-md", "Master System (SMS-0007 adapter)", "0.3.0-oscr-generated-lab", "md"
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
