// End-to-end v3 callback test against sparse /dev/slot-md fixture.
// No actual cartridge access. Paths are replaceable only in TESTING builds.
#include "retrode3/core_operations_v3.h"
#include "retrode3/sms_md_transport_v1.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" const retrode3_core_operations_v3 *retrode3_core_get_operations_v3(void);
struct Sink {
    std::vector<unsigned char> bytes;
    static int write(void *ctx, const void *v, size_t n) {
        Sink *self = static_cast<Sink *>(ctx);
        const unsigned char *p = static_cast<const unsigned char *>(v);
        self->bytes.insert(self->bytes.end(), p, p+n);
        return 0;
    }
    static int cancel(void *) { return 0; }
};
int main()
{
    char device[] = "/tmp/sms-md-live-device-XXXXXX";
    char vcc[] = "/tmp/sms-md-live-vcc-XXXXXX";
    int fd = ::mkstemp(device), vf = ::mkstemp(vcc);
    assert(fd >= 0 && vf >= 0);
    const char voltage[] = "3300mV\n";
    assert(::write(vf, voltage, sizeof(voltage) - 1) == static_cast<ssize_t>(sizeof(voltage)-1));
    ::close(vf);
    assert(::setenv("RETRODE3_SMS_MD_TEST_DEVICE", device, 1) == 0);
    assert(::setenv("RETRODE3_SMS_MD_TEST_VCC", vcc, 1) == 0);
    assert(::unsetenv("RETRODE3_SMS_MD_EXPERIMENT") == 0);
    std::vector<unsigned char> expected(32768);
    for (unsigned i=0; i<expected.size(); ++i)
        expected[i] = static_cast<unsigned char>((i/256u) ^ (i%256u));
    std::memcpy(expected.data()+0x7ff0, "TMR SEGA", 8);
    expected[0x7fff] = 0x4c;
    std::uint32_t offset = 0;
    assert(retrode3::sms::sms_read_offset(0, 512, &offset));
    // Physical MD A0 is unused by the SMS ROM: every cartridge byte
    // appears at two consecutive MD bus offsets. The production SMS
    // adapter reads 2*N positions and deinterleaves back to N bytes.
    std::vector<unsigned char> physical(expected.size() * 2u);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        physical[2u * i] = expected[i];
        physical[2u * i + 1u] = expected[i];
    }
    assert(::lseek(fd, offset, SEEK_SET) == static_cast<off_t>(offset));
    assert(::write(fd, physical.data(), physical.size()) ==
           static_cast<ssize_t>(physical.size()));
    ::close(fd);

    const retrode3_core_operations_v3 *ops = retrode3_core_get_operations_v3();
    assert(ops && ops->abi_version == 3u);
    char response[8192] = {}, error[256] = {};
    assert(!ops->fast_probe_json(response, sizeof(response), error, sizeof(error)));
    assert(std::strstr(response, "linear-32") != NULL);
    assert(std::strstr(response, "header_found\":true") != NULL);
    assert(std::strstr(response, "system_detected\":true") != NULL);
    assert(std::strstr(response, "selection_required\":false") != NULL);

    retrode3_selection_v3 s = retrode3_selection_v3();
    s.struct_size = sizeof(s); s.protocol_version = 1;
    std::strcpy(s.schema_id,"sms-md-layout-v1");
    s.mode = RETRODE3_SELECT_LAYOUT_V3;
    std::strcpy(s.layout_id,"linear-32");
    retrode3_read_plan_v3 p = retrode3_read_plan_v3();
    p.struct_size = sizeof(p);
    // Probe identifies SMS at 3.3V, but read planning stays blocked.
    assert(ops->plan_read(&s, &p, error, sizeof(error)) != 0);
    assert(::setenv("RETRODE3_SMS_MD_EXPERIMENT", "enable-readonly-v1", 1) == 0);
    assert(ops->plan_read(&s, &p, error, sizeof(error)) != 0);
    int voltage_fd = ::open(vcc, O_WRONLY | O_TRUNC);
    assert(voltage_fd >= 0);
    const char voltage5[] = "5000mV\n";
    assert(::write(voltage_fd, voltage5, sizeof(voltage5)-1) ==
           static_cast<ssize_t>(sizeof(voltage5)-1));
    ::close(voltage_fd);
    assert(!ops->plan_read(&s, &p, error, sizeof(error)));
    assert(p.payload_bytes == 32768 && p.prefix_bytes == 0);

    Sink sink;
    retrode3_host_io_v1 io = {RETRODE3_CORE_OPERATION_ABI_V1,
                              sizeof(io), &sink, Sink::write, Sink::cancel};
    retrode3_read_result_v3 result = retrode3_read_result_v3();
    result.struct_size = sizeof(result);
    assert(!ops->read_rom(&s, &io, &result, error, sizeof(error)));
    assert(sink.bytes == expected);
    assert(std::strstr(result.details_json, "\"status\":\"not_found\"") != NULL);
    assert(std::strstr(result.details_json, "\"verified\":false") != NULL);
    assert(result.payload_crc_known == 1);

    // Auto mode uses the same bounded header decoder and NEVER needs
    // an SMS-specific selection flag from the generic host.
    s.mode = RETRODE3_SELECT_AUTO_V3;
    s.layout_id[0] = '\0';
    assert(!ops->plan_read(&s, &p, error, sizeof(error)));
    assert(p.payload_bytes == 32768);
    Sink auto_sink;
    io.context = &auto_sink;
    assert(!ops->read_rom(&s, &io, &result, error, sizeof(error)));
    assert(auto_sink.bytes == expected);
    io.context = &sink;
    s.mode = RETRODE3_SELECT_LAYOUT_V3;
    std::strcpy(s.layout_id, "linear-32");

    // Invalid/unapproved mapping must fail before writing anything.
    sink.bytes.clear();
    s.mode = RETRODE3_SELECT_OVERRIDE_V3;
    s.layout_id[0] = '\0';
    s.parameter_count = 2;
    std::strcpy(s.parameters[0].id, "mapping");
    s.parameters[0].type = RETRODE3_PARAMETER_STRING_V3;
    std::strcpy(s.parameters[0].string_value,"sega16k");
    std::strcpy(s.parameters[1].id, "rom_kib");
    s.parameters[1].type = RETRODE3_PARAMETER_INTEGER_V3;
    s.parameters[1].integer_value = 128;
    assert(ops->read_rom(&s, &io, &result, error, sizeof(error)) != 0);
    assert(sink.bytes.empty());
    assert(std::strstr(error, "mapper needs explicit opt-in") != NULL);

    // Removing opt-in blocks ROM reads but NOT the header probe.
    assert(::unsetenv("RETRODE3_SMS_MD_EXPERIMENT") == 0);
    assert(!ops->fast_probe_json(response, sizeof(response), error, sizeof(error)));
    assert(std::strstr(response, "header_found\":true") != NULL);
    s.mode = RETRODE3_SELECT_LAYOUT_V3; s.parameter_count = 0;
    std::strcpy(s.layout_id, "linear-32");
    assert(ops->plan_read(&s, &p, error, sizeof(error)) != 0);

    // A fresh, independent header-only fixture simulates Action Fighter's
    // MAME-listed product code 5055 / 128 KiB without copying any game ROM.
    // The probe reads ONLY the header windows and does not bank or write.
    unsigned char header[16] = {};
    std::memcpy(header, "TMR SEGA", 8);
    header[12] = 0x55; header[13] = 0x50;
    header[14] = 1; header[15] = 0x4f;
    unsigned char doubled[32] = {};
    for (unsigned i = 0; i < 16; ++i) {
        doubled[i*2] = header[i];
        doubled[i*2+1] = header[i];
    }
    assert(retrode3::sms::sms_read_offset(0x7ff0u, 16, &offset));
    const int edit = ::open(device, O_RDWR);
    assert(edit >= 0);
    assert(::pwrite(edit, doubled, sizeof(doubled), offset) ==
           static_cast<ssize_t>(sizeof(doubled)));
    assert(::close(edit) == 0);
    assert(!ops->fast_probe_json(response, sizeof(response), error, sizeof(error)));
    assert(std::strstr(response, "\"identification_status\":\"ambiguous\"") != NULL);
    assert(std::strstr(response, "\"ambiguity\":\"titles_only\"") != NULL);
    assert(std::strstr(response, "\"candidate_lookup\":\"header_product_code\"") != NULL);
    assert(std::strstr(response, "\"title\":\"Action Fighter\"") != NULL);
    assert(std::strstr(response, "\"selection_required\":false") != NULL);
    assert(std::strstr(response, "sms-crc-3658F3E0") != NULL);
    assert(std::strstr(response, "sms-crc-D91B340D") != NULL);
    assert(std::strstr(response, "\"product_code\":5055") != NULL);
    assert(std::strstr(response, "sega16k-128") != NULL);

    // Candidate selection is validated against the current read-only header
    // (and still fails before mapper I/O with the experimental gate absent).
    s.mode = RETRODE3_SELECT_CANDIDATE_V3;
    s.layout_id[0] = '\0';
    std::strcpy(s.candidate_id, "sms-crc-3658F3E0");
    assert(::setenv("RETRODE3_SMS_MD_EXPERIMENT", "enable-readonly-v1") == 0);
    assert(ops->plan_read(&s, &p, error, sizeof(error)) != 0);
    assert(std::strstr(error, "mapper needs explicit opt-in") != NULL);
    std::strcpy(s.candidate_id, "sms-crc-00000000");
    assert(ops->plan_read(&s, &p, error, sizeof(error)) != 0);
    assert(std::strstr(error, "candidate no longer matches") != NULL);
    assert(::unsetenv("RETRODE3_SMS_MD_EXPERIMENT") == 0);

    ::unlink(device); ::unlink(vcc);
    ::unsetenv("RETRODE3_SMS_MD_TEST_DEVICE");
    ::unsetenv("RETRODE3_SMS_MD_TEST_VCC");
    return 0;
}