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
    const char voltage[] = "5000mV\n";
    assert(::write(vf, voltage, sizeof(voltage) - 1) == static_cast<ssize_t>(sizeof(voltage)-1));
    ::close(vf);
    assert(::setenv("RETRODE3_SMS_MD_TEST_DEVICE", device, 1) == 0);
    assert(::setenv("RETRODE3_SMS_MD_TEST_VCC", vcc, 1) == 0);
    assert(::setenv("RETRODE3_SMS_MD_EXPERIMENT", "enable-readonly-v1", 1) == 0);
    std::vector<unsigned char> expected(32768);
    for (unsigned i=0; i<expected.size(); ++i)
        expected[i] = static_cast<unsigned char>((i/256u) ^ (i%256u));
    std::memcpy(expected.data()+0x7ff0, "TMR SEGA", 8);
    expected[0x7fff] = 0x4c;
    std::uint32_t offset = 0;
    assert(retrode3::sms::sms_read_offset(0, 512, &offset));
    assert(::lseek(fd, offset, SEEK_SET) == static_cast<off_t>(offset));
    assert(::write(fd, expected.data(), expected.size()) ==
           static_cast<ssize_t>(expected.size()));
    ::close(fd);

    const retrode3_core_operations_v3 *ops = retrode3_core_get_operations_v3();
    assert(ops && ops->abi_version == 3u);
    char response[8192] = {}, error[256] = {};
    assert(!ops->fast_probe_json(response, sizeof(response), error, sizeof(error)));
    assert(std::strstr(response, "linear-32") != NULL);
    assert(std::strstr(response, "header_found\":true") != NULL);

    retrode3_selection_v3 s = retrode3_selection_v3();
    s.struct_size = sizeof(s); s.protocol_version = 1;
    std::strcpy(s.schema_id,"sms-md-layout-v1");
    s.mode = RETRODE3_SELECT_LAYOUT_V3;
    std::strcpy(s.layout_id,"linear-32");
    retrode3_read_plan_v3 p = retrode3_read_plan_v3();
    p.struct_size = sizeof(p);
    assert(!ops->plan_read(&s, &p, error, sizeof(error)));
    assert(p.payload_bytes == 32768 && p.prefix_bytes == 0);

    Sink sink;
    retrode3_host_io_v1 io = {RETRODE3_CORE_OPERATION_ABI_V1,
                              sizeof(io), &sink, Sink::write, Sink::cancel};
    retrode3_read_result_v3 result = retrode3_read_result_v3();
    result.struct_size = sizeof(result);
    assert(!ops->read_rom(&s, &io, &result, error, sizeof(error)));
    assert(sink.bytes == expected);
    assert(std::strstr(result.details_json, "\"status\":\"not_checked\"") != NULL);
    assert(std::strstr(result.details_json, "\"verified\":false") != NULL);

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
    assert(std::strstr(error, "bank-switch writes") != NULL);

    // Opt-in removed must deny both metadata probe and live read.
    assert(::unsetenv("RETRODE3_SMS_MD_EXPERIMENT") == 0);
    assert(!ops->fast_probe_json(response, sizeof(response), error, sizeof(error)));
    assert(std::strstr(response, "hardware_disabled") != NULL);
    s.mode = RETRODE3_SELECT_LAYOUT_V3; s.parameter_count = 0;
    std::strcpy(s.layout_id, "linear-32");
    assert(ops->plan_read(&s, &p, error, sizeof(error)) != 0);

    ::unlink(device); ::unlink(vcc);
    ::unsetenv("RETRODE3_SMS_MD_TEST_DEVICE");
    ::unsetenv("RETRODE3_SMS_MD_TEST_VCC");
    return 0;
}
