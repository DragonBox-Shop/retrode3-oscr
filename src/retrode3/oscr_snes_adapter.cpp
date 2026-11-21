// Original pinned OSCR C++ SNES checkcart and bank-read functions are linked
// separately from generated sources. This adapter contains only the Linux
// operation-ABI bridge and safety gates, never per-core NDJSON/file handling.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v3.h"
#include "retrode3/json_v3.h"
#include "retrode3/oscr_snes_compat.h"
#include "retrode3-lib.h"

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


/* Generic v3 SNES selection lives exclusively in this Retrode3 adapter.
 * Cartridge header metadata is NOT a database CRC identification. */
const char *const SNES_SCHEMA_V3 = "snes-standard-v1";

bool bounded_string(const char *data, std::size_t capacity)
{
    return std::memchr(data, '\0', capacity) != NULL;
}

std::string mapping_name(const retrode3_probe_v1 &info)
{
    return std::string(info.mapping);
}

struct PlannedSnesV3 {
    bool active;
    std::string title;
    std::string mapping;
    std::string checksum;
    std::string selected_mapping;
    std::uint64_t header_bytes;
    std::uint64_t read_bytes;
    unsigned chips;
    PlannedSnesV3() : active(false), header_bytes(0), read_bytes(0), chips(0) {}
};
PlannedSnesV3 planned_snes_v3;

int checked_probe_v3(retrode3_probe_v1 &info, char *err, size_t cap)
{
    std::memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (probe(&info, err, cap)) return -1;
    // v1 already checks chip class, original OSCR layout and size.
    if (info.rom_size_bytes < 131072u || info.rom_size_bytes > 4194304u ||
        (info.rom_size_bytes & 32767u) ||
        (std::strcmp(info.mapping, "lo") != 0 &&
         std::strcmp(info.mapping, "hi") != 0))
        return fail(err, cap, "unsupported standard SNES cartridge geometry");
    return 0;
}

int capabilities_json_v3(char *json, size_t capacity, char *err, size_t cap)
{
    static const char schema[] = R"V3({
"schema_version":1,"schema_id":"snes-standard-v1",
"hardware_access":{"version":1,
 "probe":{"module_parameters":{"legacy_bridge_hiz_test":true,"sms_mapper_write_test":false}},
 "read":{"module_parameters":{"legacy_bridge_hiz_test":true,"sms_mapper_write_test":false}}},
"selection_modes":["auto","layout","override"],
"fields":[
{"id":"mapping","label":"ROM mapping","type":"enum","required":true,
 "values":[{"value":"lo","label":"LoROM","read_supported":true},
           {"value":"hi","label":"HiROM","read_supported":true}]},
{"id":"rom_size_bytes","label":"ROM size (bytes)","type":"integer",
 "required":true,"minimum":131072,"maximum":4194304}],
"rules":[{"when":{"mapping":"lo"},"read_supported":true},
         {"when":{"mapping":"hi"},"read_supported":true}]
})V3";
    if (!json || sizeof(schema) > capacity)
        return fail(err, cap, "SNES v3 capabilities buffer too small");
    std::memcpy(json, schema, sizeof(schema));
    return 0;
}

int fast_probe_json_v3(char *json, size_t capacity, char *err, size_t cap)
{
    try {
        retrode3_probe_v1 info;
        if (checked_probe_v3(info, err, cap)) return -1;
        const bool lo = std::strcmp(info.mapping, "lo") == 0;
        std::string payload = std::string("{\"identification_status\":\"unknown\",") +
            "\"ambiguity\":\"none\",\"selection_required\":false," +
            "\"candidate_lookup\":\"cartridge_header_only\",\"candidates\":[]," +
            "\"read_layouts\":[{\"id\":\"" + (lo ? "snes-lo" : "snes-hi") +
            "\",\"label\":\"" + (lo ? "Standard LoROM" : "Standard HiROM") +
            "\",\"read_supported\":true,\"mapping\":" +
            retrode3::json_quote_v3(info.mapping) +
            ",\"size_bytes\":" + std::to_string(info.rom_size_bytes) + "}]," +
            "\"mapping\":" + retrode3::json_quote_v3(info.mapping) +
            ",\"size_bytes\":" + std::to_string(info.rom_size_bytes) +
            ",\"title\":" + retrode3::json_quote_v3(info.title) +
            ",\"checksum\":" + retrode3::json_quote_v3(info.checksum) +
            ",\"chips\":" + std::to_string(OSCR::Cores::SNES::romChips) + "}";
        if (!json || payload.size() + 1 > capacity)
            return fail(err, cap, "SNES v3 probe buffer too small");
        std::memcpy(json, payload.c_str(), payload.size() + 1);
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown SNES v3 probe error"); }
}

struct SnesReadChoiceV3 {
    std::string mapping;
    std::uint64_t size;
};

SnesReadChoiceV3 checked_selection_v3(const retrode3_selection_v3 *selection,
                                      const retrode3_probe_v1 &info)
{
    if (!selection || selection->struct_size != sizeof(*selection) ||
        selection->protocol_version != 1 ||
        !bounded_string(selection->schema_id, sizeof(selection->schema_id)) ||
        std::strcmp(selection->schema_id, SNES_SCHEMA_V3) != 0 ||
        !bounded_string(selection->candidate_id, sizeof(selection->candidate_id)) ||
        !bounded_string(selection->layout_id, sizeof(selection->layout_id)) ||
        selection->parameter_count > RETRODE3_MAX_PARAMETERS_V3)
        throw std::runtime_error("invalid or stale SNES v3 selection");
    if (selection->candidate_id[0])
        throw std::runtime_error("SNES has no verified game candidates");

    SnesReadChoiceV3 chosen;
    chosen.mapping = mapping_name(info);
    chosen.size = info.rom_size_bytes;
    if (selection->mode == RETRODE3_SELECT_AUTO_V3) {
        if (selection->layout_id[0] || selection->parameter_count)
            throw std::runtime_error("auto SNES selection contains unexpected parameters");
    } else if (selection->mode == RETRODE3_SELECT_LAYOUT_V3) {
        if (selection->parameter_count ||
            std::strcmp(selection->layout_id,
                        chosen.mapping == "lo" ? "snes-lo" : "snes-hi") != 0)
            throw std::runtime_error("SNES layout not offered by current probe");
    } else if (selection->mode == RETRODE3_SELECT_OVERRIDE_V3) {
        if (selection->layout_id[0] || selection->parameter_count != 2)
            throw std::runtime_error("manual SNES override requires mapping and rom_size_bytes");
        bool got_mapping = false, got_size = false;
        for (unsigned i = 0; i < selection->parameter_count; ++i) {
            const retrode3_parameter_v3 &p = selection->parameters[i];
            if (!bounded_string(p.id, sizeof(p.id)))
                throw std::runtime_error("invalid SNES override field");
            if (std::strcmp(p.id, "mapping") == 0) {
                if (got_mapping || p.type != RETRODE3_PARAMETER_STRING_V3 ||
                    !bounded_string(p.string_value, sizeof(p.string_value)) ||
                    (std::strcmp(p.string_value, "lo") != 0 &&
                     std::strcmp(p.string_value, "hi") != 0))
                    throw std::runtime_error("unsupported SNES override mapping");
                chosen.mapping = p.string_value;
                got_mapping = true;
            } else if (std::strcmp(p.id, "rom_size_bytes") == 0) {
                if (got_size || p.type != RETRODE3_PARAMETER_INTEGER_V3)
                    throw std::runtime_error("invalid SNES override ROM size");
                chosen.size = p.integer_value;
                got_size = true;
            } else {
                throw std::runtime_error("unknown SNES override parameter");
            }
        }
        if (!got_mapping || !got_size)
            throw std::runtime_error("incomplete SNES override");
    } else {
        throw std::runtime_error("unsupported SNES selection mode");
    }

    // Only the existing normal LoROM/HiROM, read-only bank routines are
    // allowed. No expansion chips, save RAM, ExHiROM or larger overdumps.
    // A manual mapping correction is permitted only for an otherwise
    // vetted *standard* cartridge header.
    const unsigned bank_bytes = chosen.mapping == "lo" ? 32768u : 65536u;
    if (chosen.size < 131072u || chosen.size > 4194304u ||
        chosen.size > info.rom_size_bytes || chosen.size % bank_bytes)
        throw std::runtime_error("unsupported SNES read size or bank alignment");
    const unsigned bank_count = static_cast<unsigned>(chosen.size / bank_bytes);
    const unsigned first = chosen.mapping == "lo" ?
        (chosen.size > 3145728u ? 0x80u : 0u) : 0xc0u;
    if (first + bank_count > 256u)
        throw std::runtime_error("SNES read banks outside approved address range");
    return chosen;
}

int plan_read_v3(const retrode3_selection_v3 *selection,
                 retrode3_read_plan_v3 *plan, char *err, size_t cap)
{
    try {
        planned_snes_v3.active = false;
        if (!plan || plan->struct_size != sizeof(*plan))
            return fail(err, cap, "invalid SNES v3 plan ABI");
        retrode3_probe_v1 info;
        if (checked_probe_v3(info, err, cap)) return -1;
        SnesReadChoiceV3 chosen = checked_selection_v3(selection, info);
        plan->prefix_bytes = 0;
        plan->payload_bytes = chosen.size;
        planned_snes_v3.title = info.title;
        planned_snes_v3.mapping = info.mapping;
        planned_snes_v3.checksum = info.checksum;
        planned_snes_v3.selected_mapping = chosen.mapping;
        planned_snes_v3.header_bytes = info.rom_size_bytes;
        planned_snes_v3.read_bytes = chosen.size;
        planned_snes_v3.chips = OSCR::Cores::SNES::romChips;
        planned_snes_v3.active = true;
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown SNES v3 plan error"); }
}

int read_rom_v3(const retrode3_selection_v3 *selection,
                const retrode3_host_io_v1 *io,
                retrode3_read_result_v3 *result, char *err, size_t cap)
{
    try {
        if (!io || io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
            io->struct_size < sizeof(*io) || !io->write || !io->cancelled ||
            !result || result->struct_size != sizeof(*result) ||
            !planned_snes_v3.active)
            return fail(err, cap, "invalid SNES v3 read ABI or missing plan");
        if (io->cancelled(io->context)) return fail(err, cap, "cancelled");
        retrode3_probe_v1 info;
        if (checked_probe_v3(info, err, cap)) return -1;
        SnesReadChoiceV3 chosen = checked_selection_v3(selection, info);
        if (planned_snes_v3.mapping != info.mapping ||
            planned_snes_v3.title != info.title ||
            planned_snes_v3.checksum != info.checksum ||
            planned_snes_v3.header_bytes != info.rom_size_bytes ||
            planned_snes_v3.chips != OSCR::Cores::SNES::romChips ||
            planned_snes_v3.selected_mapping != chosen.mapping ||
            planned_snes_v3.read_bytes != chosen.size)
            return fail(err, cap, "SNES cartridge or layout changed since read plan");
        planned_snes_v3.active = false;
        // Call exactly the accepted v1 bank reader using the vetted v3
        // selection. The original OSCR C++ loops remain untouched.
        retrode3_probe_v1 requested = info;
        requested.rom_size_bytes = chosen.size;
        std::snprintf(requested.mapping, sizeof(requested.mapping), "%s",
                      chosen.mapping.c_str());
        if (read_rom(&requested, io, err, cap)) return -1;
        std::string details = std::string("{\"read_configuration\":{") +
            "\"layout_id\":\"" + (chosen.mapping == "lo" ? "snes-lo" : "snes-hi") +
            "\",\"mapping\":" + retrode3::json_quote_v3(chosen.mapping) +
            ",\"rom_size_bytes\":" + std::to_string(chosen.size) +
            "},\"identity\":{\"selected_candidate_id\":null," +
            "\"matched_candidate_id\":null,\"title\":" +
            retrode3::json_quote_v3(info.title) +
            ",\"header_checksum\":" + retrode3::json_quote_v3(info.checksum) +
            "},\"verification\":{\"status\":\"not_checked\"," +
            "\"verified\":false,\"method\":\"none\",\"expected\":null,\"actual\":null}}";
        if (details.size() >= sizeof(result->details_json))
            return fail(err, cap, "SNES v3 details buffer too small");
        result->prefix_bytes = 0;
        result->payload_crc_known = 0; // Complete file CRC belongs to host.
        std::memcpy(result->details_json, details.c_str(), details.size() + 1);
        return 0;
    } catch (std::exception const &e) { return fail(err, cap, e.what()); }
    catch (...) { return fail(err, cap, "unknown SNES v3 read error"); }
}

void release()
{
    planned_snes_v3.active = false;
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
        "0.2.0-v3",
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

extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void)
{
    static const retrode3_core_operations_v3 operations = {
        RETRODE3_CORE_OPERATION_ABI_V3, sizeof(retrode3_core_operations_v3),
        capabilities_json_v3, fast_probe_json_v3, plan_read_v3, read_rom_v3
    };
    return &operations;
}
