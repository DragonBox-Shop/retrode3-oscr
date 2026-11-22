// The genuine pinned upstream OSCR MegaDrive readWord/name helpers are
// linked from generated code. This adapter only binds the safe linear
// read-only stage-1 reader to the SHARED host operation ABI. No per-core
// JSON, filesystem output, CRC32, progress, mapper writes or save support.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v3.h"
#include "retrode3/core_flash_v1.h"
#include "retrode3/json_v3.h"
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


/* Retrode3-owned generic v3 selection, sharing the physically accepted
 * linear read path. Header metadata is NOT a verified game database. */
const char *const MD_SCHEMA_V3 = "md-linear-v1";
const char *const MD_LAYOUT_V3 = "md-linear";

bool terminated(const char *data, size_t length) {
    return std::memchr(data, '\0', length) != NULL;
}

int capabilities_json_v3(char *out, size_t capacity, char *err, size_t cap)
{
    static const char schema[] = R"V3({
"schema_version":1,"schema_id":"md-linear-v1",
"selection_modes":["auto","layout","override"],
"fields":[
{"id":"mapping","label":"ROM mapping","type":"enum","required":true,
 "values":[{"value":"linear","label":"Standard linear","read_supported":true}]},
{"id":"rom_size_bytes","label":"ROM size (bytes)","type":"integer",
 "required":true,"minimum":512,"maximum":4194304}],
"rules":[{"when":{"mapping":"linear"},"read_supported":true}],
"probe_voltage_mv":3300,"read_voltage_mv":5000,
"hardware_access":{"version":1,
 "probe":{"voltage_mv":3300,"module_parameters":{"legacy_bridge_hiz_test":false,"sms_mapper_write_test":false}},
 "read":{"voltage_mv":5000,"module_parameters":{"legacy_bridge_hiz_test":false,"sms_mapper_write_test":false}}},
"automatic_voltage_switch":true
})V3";
    if (!out || sizeof(schema) > capacity)
        return fail(err, cap, "MegaDrive v3 capability buffer too small");
    std::memcpy(out, schema, sizeof(schema));
    return 0;
}

int fast_probe_json_v3(char *out, size_t capacity, char *err, size_t cap)
{
    try {
        retrode3::mdstage1::Probe info = retrode3::mdstage1::probe();
        char checksum[9] = {};
        std::snprintf(checksum, sizeof(checksum), "%04X", info.checksum);
        std::string json = std::string("{\"identification_status\":\"unknown\",") +
            "\"ambiguity\":\"none\",\"selection_required\":false," +
            "\"candidate_lookup\":\"cartridge_header_only\",\"system_detected\":true,\"candidates\":[]," +
            "\"read_layouts\":[{\"id\":\"md-linear\"," +
            "\"label\":\"Standard linear Mega Drive ROM\",\"read_supported\":true," +
            "\"size_bytes\":" + std::to_string(info.size) + "}]," +
            "\"mapping\":\"linear\",\"size_bytes\":" + std::to_string(info.size) +
            ",\"title\":" + retrode3::json_quote_v3(info.title.empty() ?
                "Mega Drive cartridge" : info.title) +
            ",\"serial\":" + retrode3::json_quote_v3(info.serial) +
            ",\"checksum\":" + retrode3::json_quote_v3(checksum) +
            ",\"save\":{\"detected\":" + (info.save.advertised ? "true" : "false") +
            ",\"kind\":" + retrode3::json_quote_v3(retrode3::mdsave::name(info.save.kind)) +
            ",\"geometry_valid\":" + (info.save.valid ? "true" : "false") +
            ",\"start_address\":" + std::to_string(info.save.start) +
            ",\"end_address\":" + std::to_string(info.save.end) +
            ",\"size_bytes\":" + std::to_string(info.save.bytes) +
            ",\"read_supported\":false,\"write_supported\":false," +
            "\"reason\":\"SRAM bus operations pending hardware validation\"}}";
        if (!out || json.size() + 1 > capacity)
            return fail(err, cap, "MegaDrive v3 probe buffer too small");
        std::memcpy(out, json.c_str(), json.size() + 1);
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown MegaDrive v3 probe error"); }
}

std::size_t checked_selection_v3(const retrode3_selection_v3 *choice,
                                 const retrode3::mdstage1::Probe &info)
{
    if (!choice || choice->struct_size != sizeof(*choice) ||
        choice->protocol_version != 1 ||
        !terminated(choice->schema_id, sizeof(choice->schema_id)) ||
        std::strcmp(choice->schema_id, MD_SCHEMA_V3) != 0 ||
        !terminated(choice->candidate_id, sizeof(choice->candidate_id)) ||
        !terminated(choice->layout_id, sizeof(choice->layout_id)) ||
        choice->parameter_count > RETRODE3_MAX_PARAMETERS_V3)
        throw std::runtime_error("invalid or stale MegaDrive v3 selection");
    if (choice->candidate_id[0])
        throw std::runtime_error("MegaDrive has no verified game candidates");
    if (choice->mode == RETRODE3_SELECT_AUTO_V3) {
        if (choice->layout_id[0] || choice->parameter_count)
            throw std::runtime_error("auto mode cannot contain MegaDrive overrides");
        return info.size;
    }
    if (choice->mode == RETRODE3_SELECT_LAYOUT_V3) {
        if (std::strcmp(choice->layout_id, MD_LAYOUT_V3) != 0 ||
            choice->parameter_count)
            throw std::runtime_error("unknown MegaDrive read layout");
        return info.size;
    }
    if (choice->mode != RETRODE3_SELECT_OVERRIDE_V3 ||
        choice->layout_id[0] || choice->parameter_count != 2)
        throw std::runtime_error("MegaDrive override requires mapping and rom_size_bytes");

    bool got_mapping = false, got_size = false;
    std::size_t requested_size = 0;
    for (unsigned i = 0; i < choice->parameter_count; ++i) {
        const retrode3_parameter_v3 &p = choice->parameters[i];
        if (!terminated(p.id, sizeof(p.id)))
            throw std::runtime_error("invalid MegaDrive override parameter name");
        if (std::strcmp(p.id, "mapping") == 0) {
            if (got_mapping || p.type != RETRODE3_PARAMETER_STRING_V3 ||
                !terminated(p.string_value, sizeof(p.string_value)) ||
                std::strcmp(p.string_value, "linear") != 0)
                throw std::runtime_error("only read-only linear MegaDrive mapping is approved");
            got_mapping = true;
        } else if (std::strcmp(p.id, "rom_size_bytes") == 0) {
            if (got_size || p.type != RETRODE3_PARAMETER_INTEGER_V3)
                throw std::runtime_error("invalid MegaDrive ROM size parameter");
            requested_size = p.integer_value;
            got_size = true;
        } else {
            throw std::runtime_error("unknown MegaDrive override parameter");
        }
    }
    // A shorter dump is allowed; expanding beyond trusted header geometry
    // or touching SRAM/special mappings is never authorized by an override.
    if (!got_mapping || !got_size || requested_size < 512u ||
        requested_size > 4194304u || (requested_size & 1u) ||
        requested_size > info.size)
        throw std::runtime_error("unsupported MegaDrive override ROM size");
    return requested_size;
}

int plan_read_v3(const retrode3_selection_v3 *choice,
                 retrode3_read_plan_v3 *plan, char *err, size_t cap)
{
    try {
        if (!plan || plan->struct_size != sizeof(*plan))
            return fail(err, cap, "invalid MegaDrive v3 plan ABI");
        retrode3::mdstage1::Probe info = retrode3::mdstage1::probe();
        plan->payload_bytes = checked_selection_v3(choice, info);
        plan->prefix_bytes = 0;
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown MegaDrive v3 plan error"); }
}

int read_rom_v3(const retrode3_selection_v3 *choice,
                const retrode3_host_io_v1 *io,
                retrode3_read_result_v3 *result, char *err, size_t cap)
{
    try {
        if (!io || io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
            io->struct_size < sizeof(*io) || !io->write || !io->cancelled ||
            !result || result->struct_size != sizeof(*result))
            return fail(err, cap, "invalid MegaDrive v3 host IO/result ABI");
        if (io->cancelled(io->context))
            return fail(err, cap, "cancelled");
        retrode3::mdstage1::Probe info = retrode3::mdstage1::probe();
        const std::size_t size = checked_selection_v3(choice, info);
        retrode3::mdstage1::read_rom(info, size, io->context,
                                    io->write, io->cancelled);
        char checksum[9] = {};
        std::snprintf(checksum, sizeof(checksum), "%04X", info.checksum);
        const std::string details = std::string("{\"read_configuration\":{") +
            "\"layout_id\":\"md-linear\",\"mapping\":\"linear\"," +
            "\"rom_size_bytes\":" + std::to_string(size) +
            "},\"identity\":{\"selected_candidate_id\":null," +
            "\"matched_candidate_id\":null,\"title\":" +
                retrode3::json_quote_v3(info.title.empty() ?
                    "Mega Drive cartridge" : info.title) +
            ",\"serial\":" + retrode3::json_quote_v3(info.serial) +
            ",\"header_checksum\":" + retrode3::json_quote_v3(checksum) +
            "},\"verification\":{\"status\":\"not_checked\"," +
            "\"verified\":false,\"method\":\"none\",\"expected\":null,\"actual\":null}}";
        if (details.size() >= sizeof(result->details_json))
            return fail(err, cap, "MegaDrive v3 result details too large");
        result->prefix_bytes = 0;
        result->payload_crc_known = 0;
        std::memcpy(result->details_json, details.c_str(), details.size() + 1);
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown MegaDrive v3 read error"); }
}

/* Advertise the emerging generic Flash ABI, but NEVER issue CFI command
 * writes until the independent kernel-bus transport is physically reviewed.
 * Read-only MD operation and SRAM probing are deliberately unchanged. */
int md_flash_capabilities(char *out, size_t capacity, char *error, size_t error_capacity)
{
    static const char data[] = R"FLASH({
"version":1,"system":"md","adapter":"megadrive",
"cfi_supported":false,"probe_supported":false,
"write_supported":false,"verify_supported":false,
"supported_command_sets":[],
"reason":"Retrode3 flash bus transport not available or hardware-verified"
})FLASH";
    if (!out || sizeof(data) > capacity)
        return fail(error, error_capacity, "MegaDrive flash capability buffer too small");
    std::memcpy(out, data, sizeof(data));
    return 0;
}
int md_flash_probe(retrode3_flash_probe_v1 *, char *error, size_t capacity)
{
    return fail(error, capacity, "MegaDrive CFI probing requires pending kernel flash transport");
}
int md_flash_plan(const retrode3_flash_probe_v1 *, uint64_t,
                  char *error, size_t capacity)
{
    return fail(error, capacity, "MegaDrive Flash write not yet enabled");
}
int md_flash_write(const retrode3_flash_probe_v1 *, uint64_t,
                   const retrode3_flash_host_io_v1 *, char *error, size_t capacity)
{
    return fail(error, capacity, "MegaDrive Flash write not yet enabled");
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
        "0.3.0-saveprobe",
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

extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void)
{
    static const retrode3_core_operations_v3 operations = {
        RETRODE3_CORE_OPERATION_ABI_V3, sizeof(retrode3_core_operations_v3),
        capabilities_json_v3, fast_probe_json_v3, plan_read_v3, read_rom_v3
    };
    return &operations;
}

extern "C" const retrode3_core_flash_operations_v1 *
retrode3_core_get_flash_operations_v1(void)
{
    static const retrode3_core_flash_operations_v1 operations = {
        RETRODE3_CORE_FLASH_ABI_V1, sizeof(retrode3_core_flash_operations_v1),
        md_flash_capabilities, md_flash_probe, md_flash_plan, md_flash_write
    };
    return &operations;
}
