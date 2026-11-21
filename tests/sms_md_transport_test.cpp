// Validates actual POSIX read-only offset translation using sparse test files.
// RETRODE3_TESTING provides fake file paths: production library ignores them.
#include "retrode3/sms_md_posix_v1.h"
#include "retrode3/sms_md_transport_v1.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

struct Sink {
    std::vector<unsigned char> got;
    static int accept(void *ctx, const void *ptr, std::size_t length) {
        Sink *self = static_cast<Sink *>(ctx);
        const unsigned char *bytes = static_cast<const unsigned char *>(ptr);
        self->got.insert(self->got.end(), bytes, bytes + length);
        return 0;
    }
};
int main()
{
    using namespace retrode3::sms;
    std::uint32_t offset = 0;
    assert(sms_read_offset(0x0000, 512, &offset));
    assert(offset == (20u << 24 | 1u << 22 | 1u << 18 | 1u << 17));
    // First SMS ROM byte (A0=0) appears at physical MD address zero.
    assert(sms_read_offset(0x0001, 1, &offset));
    assert((offset & 0xffffu) == 2u);
    assert((offset & (1u << 22)) && !(offset & (1u << 23)));
    assert(sms_read_offset(0x1ff0, 16, &offset));
    assert((offset & 0xffffu) == 0x3fe0u);
    assert(sms_read_offset(0x7ff0, 16, &offset));
    assert((offset & 0xffffu) == 0xffe0u);
    assert((offset & (1u << 18)) && (offset & (1u << 22)));
    assert(!(offset & (1u << 23)));
    assert(sms_read_offset(0x8000, 512, &offset));
    assert(!(offset & (1u << 18)) && (offset & (1u << 16)));
    assert((offset & (1u << 22)) && !(offset & (1u << 23)));
    assert(!sms_read_offset(0x7fff, 2, &offset));
    assert(!sms_read_offset(0xbfff, 2, &offset));
    assert(!sms_read_offset(0xc000, 1, &offset));
    assert(!sms_read_offset(0, 513, &offset));
    assert(!sms_read_offset(0, 1, NULL));

    char device[] = "/tmp/sms-md-bus-XXXXXX";
    char vcc[] = "/tmp/sms-md-vcc-XXXXXX";
    int fd = ::mkstemp(device);
    int vf = ::mkstemp(vcc);
    assert(fd >= 0 && vf >= 0);
    assert(::setenv("RETRODE3_SMS_MD_TEST_DEVICE", device, 1) == 0);
    assert(::setenv("RETRODE3_SMS_MD_TEST_VCC", vcc, 1) == 0);
    const char volt3[] = "3300mV\n";
    assert(::write(vf, volt3, sizeof(volt3) - 1) == static_cast<ssize_t>(sizeof(volt3)-1));
    std::vector<unsigned char> expected(32768);
    for (unsigned i = 0; i < expected.size(); ++i)
        expected[i] = static_cast<unsigned char>((i * 7u + 3u) & 0xffu);
    std::memcpy(&expected[0x7ff0], "TMR SEGA", 8);
    expected[0x7fff] = 0x4cu; // EU / 32-KiB header
    assert(sms_read_offset(0, 512, &offset));
    // Model a real MD slot: physical A0 is irrelevant to SMS. Therefore
    // each SMS ROM byte is observed at two consecutive MD bus addresses.
    std::vector<unsigned char> physical(expected.size() * 2u);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        physical[2u * i] = expected[i];
        physical[2u * i + 1u] = expected[i];
    }
    assert(::lseek(fd, offset, SEEK_SET) == static_cast<off_t>(offset));
    assert(::write(fd, physical.data(), physical.size()) ==
           static_cast<ssize_t>(physical.size()));
    ::close(fd);

    assert(::unsetenv("RETRODE3_SMS_MD_EXPERIMENT") == 0);
    SmsMdPosixBus bus;
    SmsMdPosixBus probe_bus(NULL, NULL, SmsMdHeaderProbe);
    assert(probe_bus.approved()); // 3.3V header reads need no opt-in.
    const Probe low_voltage_probe = bounded_probe(probe_bus);
    assert(low_voltage_probe.header_found && !low_voltage_probe.conflicting_headers);
    assert(low_voltage_probe.header.declared_rom_bytes == 32768u);
    assert(!bus.approved());
    assert(::setenv("RETRODE3_SMS_MD_EXPERIMENT", "wrong", 1) == 0);
    assert(!bus.approved());
    assert(::setenv("RETRODE3_SMS_MD_EXPERIMENT", "enable-readonly-v1", 1) == 0);
    assert(!bus.approved()); // 3.3V is never accepted for ROM streaming
    assert(::ftruncate(vf, 0) == 0);
    assert(::lseek(vf, 0, SEEK_SET) == 0);
    const char volt5[] = "5000mV\n";
    assert(::write(vf, volt5, sizeof(volt5) - 1) == static_cast<ssize_t>(sizeof(volt5)-1));
    ::close(vf);
    assert(bus.approved());
    assert(probe_bus.approved());

    const Probe probe = bounded_probe(bus);
    assert(probe.header_found && !probe.conflicting_headers);
    assert(probe.address == 0x7ff0);
    assert(probe.header.declared_rom_bytes == 32768u);
    Sink sink;
    const Layout layout = {Linear, 32};
    stream_rom(bus, layout, &sink, Sink::accept);
    assert(sink.got == expected);
    assert(!bus.begin_sega_banked()); // no mapper writes
    assert(!bus.select_page2(1));
    assert(!bus.end_sega_banked());
    assert(::unsetenv("RETRODE3_SMS_MD_EXPERIMENT") == 0);
    assert(!bus.approved());
    assert(probe_bus.approved()); // Same header path remains available.
    ::unlink(device);
    ::unlink(vcc);
    ::unsetenv("RETRODE3_SMS_MD_EXPERIMENT");
    ::unsetenv("RETRODE3_SMS_MD_TEST_DEVICE");
    ::unsetenv("RETRODE3_SMS_MD_TEST_VCC");
    return 0;
}