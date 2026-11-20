// Explicitly opted-in read-only bus transport for SMS-0007 on MD slot.
// Does not perform any cartridge writes or power switching.
#include "retrode3/sms_md_posix_v1.h"
#include "retrode3/sms_md_transport_v1.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/types.h>
#include <unistd.h>

namespace retrode3 { namespace sms {
namespace {
const char *const kDevice = "/dev/slot-md";
const char *const kVcc = "/sys/class/retrode3/slot-md/vcc";
const char *const kOptIn = "enable-readonly-v1";
const char *const kOptInVar = "RETRODE3_SMS_MD_EXPERIMENT";

const char *device_path()
{
#ifdef RETRODE3_TESTING
    const char *p = std::getenv("RETRODE3_SMS_MD_TEST_DEVICE");
    if (p && p[0]) return p;
#endif
    return kDevice;
}
const char *vcc_path()
{
#ifdef RETRODE3_TESTING
    const char *p = std::getenv("RETRODE3_SMS_MD_TEST_VCC");
    if (p && p[0]) return p;
#endif
    return kVcc;
}
} // namespace

bool sms_experiment_enabled()
{
    const char *s = std::getenv(kOptInVar);
    return s && std::strcmp(s, kOptIn) == 0;
}

bool sms_voltage_is_5v()
{
    FILE *file = std::fopen(vcc_path(), "r");
    if (!file) return false;
    char line[32] = {};
    const bool ok = std::fgets(line, sizeof(line), file) != NULL;
    std::fclose(file);
    return ok && std::strcmp(line, "5000mV\n") == 0;
}

SmsMdPosixBus::SmsMdPosixBus(CancelFn cancel, void *context)
    : fd_(-1), cancel_(cancel), context_(context)
{
}
SmsMdPosixBus::~SmsMdPosixBus()
{
    if (fd_ >= 0) ::close(fd_);
}

bool SmsMdPosixBus::approved() const
{
    return sms_experiment_enabled() && sms_voltage_is_5v();
}
bool SmsMdPosixBus::cancelled() const
{
    return cancel_ && cancel_(context_) != 0;
}
bool SmsMdPosixBus::read(std::uint16_t address, std::uint8_t *bytes,
                         std::size_t count)
{
    std::uint32_t offset = 0;
    if (!approved() || !bytes || !sms_read_offset(address, count, &offset) ||
        cancelled()) return false;
    if (fd_ < 0) {
        fd_ = ::open(device_path(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd_ < 0) return false;
    }
    if (::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) !=
        static_cast<off_t>(offset)) return false;
    std::size_t total = 0;
    while (total < count) {
        if (cancelled()) return false;
        const ssize_t n = ::read(fd_, bytes + total, count - total);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        total += static_cast<std::size_t>(n);
    }
    return true;
}

bool SmsMdPosixBus::begin_sega_banked() { return false; }
bool SmsMdPosixBus::select_page2(std::uint8_t) { return false; }
bool SmsMdPosixBus::end_sega_banked() { return false; }

} } // retrode3::sms
