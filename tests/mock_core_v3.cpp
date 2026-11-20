// Synthetic, hardware-free v3 core for generic contract regression tests.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v3.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
int fail(char *err, size_t cap, const char *why) {
    if (err && cap) std::snprintf(err, cap, "%s", why);
    return -1;
}
const char *scenario() {
    const char *s = std::getenv("RETRODE_TEST_V3_SCENARIO");
    return s ? s : "titles";
}
int copy(const std::string &s, char *out, size_t cap, char *err, size_t ec) {
    if (s.size() + 1 > cap) return fail(err, ec, "JSON buffer too small");
    std::memcpy(out, s.c_str(), s.size() + 1);
    return 0;
}
int cap_json(char *out, size_t n, char *err, size_t ec) {
    return copy("{\"schema_version\":1,\"schema_id\":\"test-v1\","
        "\"selection_modes\":[\"auto\",\"candidate\",\"layout\",\"override\"],"
        "\"fields\":[{\"id\":\"bank_count\",\"label\":\"Banks\","
        "\"type\":\"integer\",\"minimum\":1,\"maximum\":2},"
        "{\"id\":\"safe\",\"label\":\"Safe\",\"type\":\"boolean\"}],"
        "\"rules\":[{\"when\":{\"bank_count\":2},\"read_supported\":false}]}",
        out, n, err, ec);
}
int probe_json(char *out, size_t n, char *err, size_t ec) {
    std::string s(scenario());
    const char *head = "{\"identification_status\":";
    if (s == "unknown") {
        return copy(std::string(head) + "\"unknown\",\"ambiguity\":\"none\","
            "\"selection_required\":true,\"candidates\":[],\"read_layouts\":[]}",
            out, n, err, ec);
    }
    if (s == "unique") {
        return copy(std::string(head) + "\"unique\",\"ambiguity\":\"none\","
            "\"selection_required\":false,\"candidates\":["
            "{\"id\":\"a\",\"title\":\"Game A\",\"layout_id\":\"x\","
            "\"read_supported\":true}],\"read_layouts\":["
            "{\"id\":\"x\",\"read_supported\":true}]}",
            out, n, err, ec);
    }
    bool different = s == "layouts";
    bool blocked = s == "blocked";
    return copy(std::string(head) + "\"ambiguous\",\"ambiguity\":\"" +
        (different ? "layouts" : "titles_only") +
        "\",\"selection_required\":" + (different ? "true" : "false") +
        ",\"candidates\":["
        "{\"id\":\"a\",\"title\":\"Game A\",\"layout_id\":\"x\","
        "\"read_supported\":true},"
        "{\"id\":\"b\",\"title\":\"Game B\",\"layout_id\":\"" +
        (different ? "y" : "x") +
        "\",\"read_supported\":" + (blocked ? "false" : "true") +
        (blocked ? ",\"reason\":\"unapproved\"}" : "}") +
        "],\"read_layouts\":[{\"id\":\"x\",\"read_supported\":true}" +
        (different ? ",{\"id\":\"y\",\"read_supported\":true}" : "") +
        "]}", out, n, err, ec);
}
bool valid(const retrode3_selection_v3 *c, char *err, size_t ec) {
    if (!c || c->struct_size != sizeof(*c) || c->protocol_version != 1 ||
        std::strcmp(c->schema_id, "test-v1") != 0 ||
        c->parameter_count > RETRODE3_MAX_PARAMETERS_V3) {
        fail(err, ec, "bad selection ABI or schema");
        return false;
    }
    const std::string s(scenario());
    if (c->mode == RETRODE3_SELECT_AUTO_V3) {
        if (s == "layouts" || s == "unknown" || c->parameter_count ||
            c->candidate_id[0] || c->layout_id[0]) {
            fail(err, ec, "selection required");
            return false;
        }
        return true;
    }
    if (c->mode == RETRODE3_SELECT_CANDIDATE_V3) {
        if (c->parameter_count || c->layout_id[0] ||
            (std::strcmp(c->candidate_id, "a") &&
             std::strcmp(c->candidate_id, "b")) ||
            s == "unknown" ||
            (s == "blocked" && !std::strcmp(c->candidate_id, "b"))) {
            fail(err, ec, "candidate unavailable");
            return false;
        }
        return true;
    }
    if (c->mode == RETRODE3_SELECT_LAYOUT_V3) {
        if (c->parameter_count || c->candidate_id[0] ||
            (std::strcmp(c->layout_id, "x") &&
             !(s == "layouts" && !std::strcmp(c->layout_id, "y")))) {
            fail(err, ec, "layout unavailable");
            return false;
        }
        return true;
    }
    if (c->mode != RETRODE3_SELECT_OVERRIDE_V3 || c->candidate_id[0] ||
        c->layout_id[0] || c->parameter_count != 2) {
        fail(err, ec, "bad override");
        return false;
    }
    bool banks = false, safe = false;
    for (unsigned i = 0; i < c->parameter_count; ++i) {
        const retrode3_parameter_v3 &p = c->parameters[i];
        if (!std::strcmp(p.id, "bank_count")) {
            banks = p.type == RETRODE3_PARAMETER_INTEGER_V3 &&
                    p.integer_value == 1;
        } else if (!std::strcmp(p.id, "safe")) {
            safe = p.type == RETRODE3_PARAMETER_BOOLEAN_V3 &&
                   p.integer_value == 1;
        } else {
            fail(err, ec, "unknown override parameter");
            return false;
        }
    }
    if (!banks || !safe) {
        fail(err, ec, "override not approved");
        return false;
    }
    return true;
}
int plan(const retrode3_selection_v3 *choice, retrode3_read_plan_v3 *p,
         char *err, size_t n) {
    if (!p || p->struct_size != sizeof(*p) || !valid(choice, err, n))
        return -1;
    p->prefix_bytes = 4;
    p->payload_bytes = 1024;
    return 0;
}
int read(const retrode3_selection_v3 *choice, const retrode3_host_io_v1 *io,
         retrode3_read_result_v3 *r, char *err, size_t n) {
    if (!valid(choice, err, n) || !io || !io->write || !io->cancelled ||
        !r || r->struct_size != sizeof(*r))
        return fail(err, n, "invalid v3 read");
    unsigned char bytes[128] = {};
    for (unsigned i = 0; i < 8; ++i) {
        if (io->cancelled(io->context)) return fail(err, n, "cancelled");
        if (io->write(io->context, bytes, sizeof(bytes)))
            return fail(err, n, "write failed or cancelled");
    }
    r->prefix_bytes = 4;
    std::memcpy(r->prefix, "TEST", 4);
    r->payload_crc_known = 0;
    const bool mismatch = choice->mode == RETRODE3_SELECT_CANDIDATE_V3 &&
                          !std::strcmp(choice->candidate_id, "b");
    const std::string details =
        std::string("{\"read_configuration\":{\"bank_count\":1},"
        "\"identity\":{\"selected_candidate_id\":") +
        (choice->mode == RETRODE3_SELECT_CANDIDATE_V3 ?
         (mismatch ? "\"b\"" : "\"a\"") : "null") +
        ",\"matched_candidate_id\":\"a\",\"title\":\"Game A\"},"
        "\"verification\":{\"status\":\"" +
        (mismatch ? "mismatch" : "not_found") +
        "\",\"verified\":false}}";
    if (details.size() >= sizeof(r->details_json))
        return fail(err, n, "result metadata too long");
    std::memcpy(r->details_json, details.c_str(), details.size() + 1);
    return 0;
}
int v1_probe(retrode3_probe_v1 *p, char *, size_t) {
    if (!p || p->struct_size != sizeof(*p)) return -1;
    p->rom_size_bytes = 1024;
    std::strcpy(p->title, "GENERIC FIXTURE");
    std::strcpy(p->mapping, "linear");
    return 0;
}
void release() {}
}
extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void) {
    static const retrode3_core_descriptor_v1 d = {
        RETRODE3_CORE_DISCOVERY_ABI_V1, sizeof(d),
        "mock-v3", "Generic v3 fixture", "1-test", "snes"
    };
    return &d;
}
extern "C" const retrode3_core_operations_v1 *retrode3_core_get_operations_v1(void) {
    static const retrode3_core_operations_v1 ops = {
        RETRODE3_CORE_OPERATION_ABI_V1, sizeof(ops), v1_probe, NULL, release
    };
    return &ops;
}
extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void) {
    static const retrode3_core_operations_v3 ops = {
        RETRODE3_CORE_OPERATION_ABI_V3, sizeof(ops),
        cap_json, probe_json, plan, read
    };
    return &ops;
}
