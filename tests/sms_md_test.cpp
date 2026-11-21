#include "retrode3/sms_rom_core_v1.h"
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v3.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void);
extern "C" const retrode3_core_operations_v1 *retrode3_core_get_operations_v1(void);
extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void);

using retrode3::sms::Bus;
using retrode3::sms::Layout;

struct Fixture : Bus {
    std::vector<unsigned char> rom;
    bool permitted, failed_read, failed_bank, cancelled_flag, configured, ended;
    unsigned reads, selects;
    unsigned current_bank;
    Fixture(unsigned kib) : rom(kib * 1024u), permitted(true),
        failed_read(false), failed_bank(false), cancelled_flag(false),
        configured(false), ended(false), reads(0), selects(0), current_bank(0) {
        for (unsigned i=0; i<rom.size(); ++i)
            rom[i] = static_cast<unsigned char>((i / 16384u) ^ ((i % 16384u) / 512u));
    }
    bool approved() const { return permitted; }
    bool read(std::uint16_t addr, std::uint8_t *dest, std::size_t n) {
        if (failed_read || !permitted) return false;
        unsigned origin = configured ? current_bank * 16384u + addr - 0x8000u : addr;
        if (configured && (addr < 0x8000u || addr + n > 0xc000u)) return false;
        if (origin + n > rom.size()) return false;
        std::memcpy(dest, &rom[origin], n);
        ++reads;
        return true;
    }
    bool begin_sega_banked() { configured = true; ended = false; return true; }
    bool select_page2(std::uint8_t bank) {
        if (failed_bank) return false;
        ++selects;
        current_bank = bank;
        return true;
    }
    bool end_sega_banked() { ended = true; configured = false; return true; }
    bool cancelled() const { return cancelled_flag; }
};

struct Sink {
    std::vector<unsigned char> bytes;
    unsigned writes;
    bool reject;
    Sink() : writes(0), reject(false) {}
    static int write(void *ctx, const void *data, std::size_t n) {
        Sink &sink = *static_cast<Sink *>(ctx);
        ++sink.writes;
        if (sink.reject) return -1;
        const unsigned char *b = static_cast<const unsigned char *>(data);
        sink.bytes.insert(sink.bytes.end(), b, b + n);
        return 0;
    }
};

bool throws(Fixture &bus, Layout layout, Sink &sink) {
    try { retrode3::sms::stream_rom(bus, layout, &sink, Sink::write); }
    catch (const std::exception &) { return true; }
    return false;
}

int main() {
    const unsigned valid_linear[] = {8u, 16u, 32u};
    for (unsigned i = 0; i < 3; ++i) {
        Fixture bus(valid_linear[i]); Sink sink;
        Layout l = {retrode3::sms::Linear, valid_linear[i]};
        assert(retrode3::sms::valid_layout(l));
        retrode3::sms::stream_rom(bus, l, &sink, Sink::write);
        assert(sink.bytes == bus.rom);
        assert(!bus.configured && bus.selects == 0);
        assert(bus.reads == valid_linear[i] * 2u);
    }
    const unsigned valid_banked[] = {48u, 64u, 128u, 256u, 512u, 1024u};
    for (unsigned i=0; i<6; ++i) {
        Fixture bus(valid_banked[i]); Sink sink;
        Layout l = {retrode3::sms::Sega16k, valid_banked[i]};
        assert(retrode3::sms::valid_layout(l));
        retrode3::sms::stream_rom(bus, l, &sink, Sink::write);
        assert(sink.bytes == bus.rom);
        assert(bus.selects == valid_banked[i] / 16u);
        assert(bus.ended && !bus.configured);
    }
    {
        Layout bad = {retrode3::sms::Linear, 64u};
        assert(!retrode3::sms::valid_layout(bad));
        Fixture bus(64); Sink sink;
        assert(throws(bus, bad, sink));
        assert(bus.reads == 0);
        bad.mapping = retrode3::sms::Sega16k; bad.rom_kib = 32u;
        assert(throws(bus, bad, sink));
    }
    {
        Fixture bus(64); Sink sink;
        bus.permitted = false;
        assert(throws(bus, Layout{retrode3::sms::Sega16k, 64u}, sink));
        assert(!bus.configured && bus.selects == 0);
    }
    {
        Fixture bus(64); Sink sink;
        sink.reject = true;
        assert(throws(bus, Layout{retrode3::sms::Sega16k, 64u}, sink));
        assert(bus.ended && sink.bytes.empty());
    }
    {
        Fixture bus(64); Sink sink;
        bus.failed_bank = true;
        assert(throws(bus, Layout{retrode3::sms::Sega16k, 64u}, sink));
        assert(bus.ended && sink.bytes.empty());
    }
    {
        Fixture bus(64); Sink sink;
        bus.cancelled_flag = true;
        assert(throws(bus, Layout{retrode3::sms::Sega16k, 64u}, sink));
        assert(bus.ended && sink.bytes.empty());
    }
    {
        Fixture bus(64);
        for (unsigned i=0; i<3; ++i) {
            unsigned start = (i==0 ? 0x1ff0u : (i==1 ? 0x3ff0u : 0x7ff0u));
            std::memcpy(&bus.rom[start], "TMR SEGA", 8);
            bus.rom[start+10] = 0x34;
            bus.rom[start+11] = 0x12;
            bus.rom[start+15] = 0x4eu;
        }
        retrode3::sms::Probe probe = retrode3::sms::bounded_probe(bus);
        assert(probe.header_found && !probe.conflicting_headers);
        assert(probe.address == 0x1ff0);
        assert(probe.header.checksum == 0x1234);
        assert(probe.header.declared_rom_bytes == 65536u);
        assert(bus.reads == 3);
        bus.rom[0x7fff] = 0x4fu;
        retrode3::sms::Probe mismatch = retrode3::sms::bounded_probe(bus);
        assert(mismatch.conflicting_headers);
    }
    {
        const retrode3_core_descriptor_v1 *d = retrode3_core_describe_v1();
        assert(d && std::strcmp(d->id, "sms-md") == 0 &&
               std::strcmp(d->slot, "md") == 0);
        const retrode3_core_operations_v1 *old = retrode3_core_get_operations_v1();
        assert(old && old->read_rom == NULL && old->probe && old->release);
        const retrode3_core_operations_v3 *v3 = retrode3_core_get_operations_v3();
        assert(v3 && v3->abi_version == 3 && v3->capabilities_json &&
               v3->fast_probe_json && v3->plan_read && v3->read_rom);
        char json[8192], err[256];
        assert(v3->capabilities_json(json, sizeof(json), err, sizeof(err)) == 0);
        assert(std::strstr(json, "sms-md-layout-v1") != NULL);
        assert(std::strstr(json, "read_supported\":true") != NULL);
        /*
         * This policy test must never probe the physical MD slot.
         * The TESTING-only voltage path points at /dev/null, so the
         * unavailable-hardware response is deterministic on a host
         * workstation AND on a Retrode with a live SMS cartridge.
         */
        assert(::setenv("RETRODE3_SMS_MD_TEST_VCC", "/dev/null", 1) == 0);
        assert(v3->fast_probe_json(json, sizeof(json), err, sizeof(err)) == 0);
        assert(std::strstr(json, "hardware_unavailable") != NULL);
        assert(::unsetenv("RETRODE3_SMS_MD_TEST_VCC") == 0);
        retrode3_selection_v3 selection = retrode3_selection_v3();
        selection.struct_size = sizeof(selection);
        selection.protocol_version = 1;
        selection.mode = RETRODE3_SELECT_OVERRIDE_V3;
        std::strcpy(selection.schema_id, "sms-md-layout-v1");
        selection.parameter_count = 2;
        std::strcpy(selection.parameters[0].id, "mapping");
        selection.parameters[0].type = RETRODE3_PARAMETER_STRING_V3;
        std::strcpy(selection.parameters[0].string_value, "linear");
        std::strcpy(selection.parameters[1].id, "rom_kib");
        selection.parameters[1].type = RETRODE3_PARAMETER_INTEGER_V3;
        selection.parameters[1].integer_value = 32;
        retrode3_read_plan_v3 plan = retrode3_read_plan_v3();
        plan.struct_size = sizeof(plan);
        assert(v3->plan_read(&selection, &plan, err, sizeof(err)) != 0);
        assert(std::strstr(err, "explicit opt-in") != NULL);
        selection.parameters[1].integer_value = 64;
        assert(v3->plan_read(&selection, &plan, err, sizeof(err)) != 0);
        assert(std::strstr(err, "unsupported") != NULL);
        selection.parameters[1].integer_value = 32;
        selection.parameter_count = 3;
        assert(v3->plan_read(&selection, &plan, err, sizeof(err)) != 0);
        selection.parameter_count = 2;
        std::strcpy(selection.parameters[0].id, "voltage");
        assert(v3->plan_read(&selection, &plan, err, sizeof(err)) != 0);
        assert(std::strstr(err, "unknown") != NULL);
        assert(v3->capabilities_json(json, 1, err, sizeof(err)) != 0);

        // Even a structurally valid direct read must never reach the sink.
        unsigned writes = 0;
        struct Writer {
            static int send(void *ctx, const void *, std::size_t) {
                ++*static_cast<unsigned *>(ctx);
                return 0;
            }
            static int cancelled(void *) { return 0; }
        };
        retrode3_host_io_v1 io = retrode3_host_io_v1();
        io.abi_version = RETRODE3_CORE_OPERATION_ABI_V1;
        io.struct_size = sizeof(io);
        io.context = &writes;
        io.write = Writer::send;
        io.cancelled = Writer::cancelled;
        retrode3_read_result_v3 result = retrode3_read_result_v3();
        result.struct_size = sizeof(result);
        std::strcpy(selection.parameters[0].id, "mapping");
        assert(v3->read_rom(&selection, &io, &result, err, sizeof(err)) != 0);
        assert(writes == 0);
        retrode3_probe_v1 legacy = retrode3_probe_v1();
        legacy.struct_size = sizeof(legacy);
        assert(old->probe(&legacy, err, sizeof(err)) != 0);
        assert(writes == 0);
    }
    return 0;
}