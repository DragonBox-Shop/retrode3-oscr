// Synthetic Flash core for the experimental shared-CLI contract.
// No GPIO or real cartridge I/O, and never part of release staging.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v3.h"
#include "retrode3/core_flash_v1.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::vector<unsigned char> &memory() {
    static std::vector<unsigned char> contents(4194304u, 0xff);
    return contents;
}
int fail(char *message, size_t cap, const char *why) {
    if (message && cap)
        std::snprintf(message, cap, "%s", why);
    return -1;
}
int meta(char *out, size_t count, char *err, size_t cap) {
    const char *json = "{\"schema_version\":1,\"schema_id\":\"flash-fixture-v1\","
        "\"selection_modes\":[\"auto\"],\"fields\":[],\"rules\":[]}";
    if (std::strlen(json) >= count)
        return fail(err, cap, "small schema buffer");
    std::strcpy(out, json);
    return 0;
}
int fast_probe(char *out, size_t count, char *err, size_t cap) {
    const char *json = "{\"system_detected\":true,"
        "\"identification_status\":\"unknown\",\"ambiguity\":\"none\","
        "\"selection_required\":false,\"candidates\":[],\"read_layouts\":[]}";
    if (std::strlen(json) >= count)
        return fail(err, cap, "small probe buffer");
    std::strcpy(out, json);
    return 0;
}
int plan(const retrode3_selection_v3 *, retrode3_read_plan_v3 *,
         char *err, size_t cap) {
    return fail(err, cap, "Flash fixture does not dump ROMs");
}
int read(const retrode3_selection_v3 *, const retrode3_host_io_v1 *,
         retrode3_read_result_v3 *, char *err, size_t cap) {
    return fail(err, cap, "Flash fixture does not dump ROMs");
}
int v1_probe(retrode3_probe_v1 *p, char *, size_t) {
    if (!p || p->struct_size != sizeof(*p))
        return -1;
    p->rom_size_bytes = 0;
    std::strcpy(p->title, "Flash fixture");
    std::strcpy(p->mapping, "linear");
    return 0;
}
void release() {}
int identify(retrode3_flash_identity_v1 *id, char *err, size_t cap) {
    if (!id || id->struct_size != sizeof(*id))
        return fail(err, cap, "invalid identify ABI");
    if (std::getenv("RETRODE3_TEST_FLASH_BAD_ID"))
        return fail(err, cap, "unsupported test Flash identity");
    id->manufacturer_id = 0x7f;
    id->device_id = 0x22f9;
    id->size_bytes = 4194304u;
    id->command_set = 2;
    id->region_count = 2;
    id->regions[0].start_bytes = 0;
    id->regions[0].sector_bytes = 8192;
    id->regions[0].count = 8;
    id->regions[1].start_bytes = 65536;
    id->regions[1].sector_bytes = 65536;
    id->regions[1].count = 63;
    return 0;
}
int erase(uint32_t addr, char *err, size_t cap) {
    const uint32_t bytes = addr < 65536u ? 8192u : 65536u;
    if (addr >= memory().size() || addr + bytes > memory().size() ||
        (addr < 65536u ? (addr % 8192u) : ((addr - 65536u) % 65536u)))
        return fail(err, cap, "invalid sector address");
    if (std::getenv("RETRODE3_TEST_FLASH_FAIL_ERASE"))
        return fail(err, cap, "mock erase failed");
    std::memset(&memory()[addr], 0xff, bytes);
    return 0;
}
int program(uint32_t addr, const void *bytes, size_t n, char *err, size_t cap) {
    if (!bytes || !n || n > 512 || (n & 1u) || (addr & 1u) ||
        addr > memory().size() - n)
        return fail(err, cap, "invalid chunk");
    if (std::getenv("RETRODE3_TEST_FLASH_FAIL_PROGRAM"))
        return fail(err, cap, "mock program failed");
    const unsigned char *p = static_cast<const unsigned char *>(bytes);
    for (size_t i = 0; i < n; ++i) {
        if ((memory()[addr+i] & p[i]) != p[i])
            return fail(err, cap, "mock requires erase");
        memory()[addr+i] = p[i];
    }
    return 0;
}
int readback(uint32_t addr, void *bytes, size_t n, char *err, size_t cap) {
    if (!bytes || !n || addr >= memory().size() ||
        n > memory().size() - addr)
        return fail(err, cap, "invalid readback");
    std::memcpy(bytes, &memory()[addr], n);
    if (std::getenv("RETRODE3_TEST_FLASH_MISMATCH") && addr <= 7 &&
        n > 7 - addr)
        static_cast<unsigned char *>(bytes)[7 - addr] ^= 0x01;
    return 0;
}
}
extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void) {
    static const retrode3_core_descriptor_v1 d = {
        RETRODE3_CORE_DISCOVERY_ABI_V1, sizeof(d),
        "flash-mock", "Flash fixture", "1-test", "md"
    };
    return &d;
}
extern "C" const retrode3_core_operations_v1 *retrode3_core_get_operations_v1(void) {
    static const retrode3_core_operations_v1 o = {
        RETRODE3_CORE_OPERATION_ABI_V1, sizeof(o), v1_probe, NULL, release
    };
    return &o;
}
extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void) {
    static const retrode3_core_operations_v3 o = {
        RETRODE3_CORE_OPERATION_ABI_V3, sizeof(o),
        meta, fast_probe, plan, read
    };
    return &o;
}
extern "C" const retrode3_core_flash_operations_v1 *
retrode3_core_get_flash_operations_v1(void) {
    static const retrode3_core_flash_operations_v1 f = {
        RETRODE3_CORE_FLASH_ABI_V1, sizeof(f),
        identify, erase, program, readback
    };
    return &f;
}
