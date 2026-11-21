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
#include <vector>

namespace retrode3 { namespace sms {
namespace {
const char *const kDevice = "/dev/slot-md";
const char *const kVcc = "/sys/class/retrode3/slot-md/vcc";
const char *const kOptIn = "enable-readonly-v1";
const char *const kOptInVar = "RETRODE3_SMS_MD_EXPERIMENT";
const char *const kMapperOptInVar = "RETRODE3_SMS_MD_MAPPER_EXPERIMENT";
const char *const kMapperOptIn = "enable-sega16k-v1";
const char *const kKernelMapperGate = "/sys/module/retrode3/parameters/sms_mapper_write_test";
const char *const kKernelHizGate = "/sys/module/retrode3/parameters/legacy_bridge_hiz_test";

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
const char *mapper_gate_path()
{
#ifdef RETRODE3_TESTING
    const char *p = std::getenv("RETRODE3_SMS_MD_TEST_MAPPER_GATE");
    if (p && p[0]) return p;
#endif
    return kKernelMapperGate;
}
const char *hiz_gate_path()
{
#ifdef RETRODE3_TESTING
    const char *p = std::getenv("RETRODE3_SMS_MD_TEST_HIZ_GATE");
    if (p && p[0]) return p;
#endif
    return kKernelHizGate;
}
bool parameter_enabled(const char *path)
{
    FILE *file = std::fopen(path, "r");
    if (!file) return false;
    char line[16] = {};
    const bool ok = std::fgets(line, sizeof(line), file) != NULL;
    std::fclose(file);
    return ok && (line[0] == 'Y' || line[0] == '1') &&
           (line[1] == '\n' || line[1] == '\0');
}
} // namespace

bool sms_experiment_enabled()
{
    const char *s = std::getenv(kOptInVar);
    return s && std::strcmp(s, kOptIn) == 0;
}
bool sms_mapper_experiment_enabled()
{
    const char *s = std::getenv(kMapperOptInVar);
    return sms_experiment_enabled() && s &&
           std::strcmp(s, kMapperOptIn) == 0;
}
bool sms_kernel_mapper_gate_enabled()
{
    return parameter_enabled(mapper_gate_path()) &&
           parameter_enabled(hiz_gate_path());
}

namespace {
bool voltage_matches(bool allow_3v3)
{
    FILE *file = std::fopen(vcc_path(), "r");
    if (!file) return false;
    char line[32] = {};
    const bool ok = std::fgets(line, sizeof(line), file) != NULL;
    std::fclose(file);
    return ok && (std::strcmp(line, "5000mV\n") == 0 ||
                  (allow_3v3 && std::strcmp(line, "3300mV\n") == 0));
}
} // namespace

bool sms_voltage_is_5v() { return voltage_matches(false); }
bool sms_voltage_allows_probe() { return voltage_matches(true); }

SmsMdPosixBus::SmsMdPosixBus(CancelFn cancel, void *context,
                             SmsMdAccess access)
    : fd_(-1), cancel_(cancel), context_(context), access_(access),
      mapper_started_(false)
{
}
SmsMdPosixBus::~SmsMdPosixBus()
{
    if (mapper_started_) (void)end_sega_banked();
    if (fd_ >= 0) ::close(fd_);
}

bool SmsMdPosixBus::approved() const
{
    if (access_ == SmsMdHeaderProbe) return sms_voltage_allows_probe();
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
    // MD physical A1 is SMS A0. Consecutive MD addresses therefore
    // sample each SMS byte twice. Read twice the number of bus positions,
    // then keep the first sample from each adjacent pair.
    // sms_read_offset() restricts count to 512 bytes and checks boundaries.
    std::vector<std::uint8_t> physical(count * 2u);
    std::size_t total = 0;
    while (total < physical.size()) {
        if (cancelled()) return false;
        const ssize_t n = ::read(fd_, &physical[total],
                                 physical.size() - total);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        total += static_cast<std::size_t>(n);
    }
    for (std::size_t i = 0; i < count; ++i)
        bytes[i] = physical[i * 2u];
    return true;
}

bool SmsMdPosixBus::mapper_write(std::uint16_t reg, std::uint8_t value)
{
    std::uint32_t offset = 0;
    if (!sms_mapper_write_offset(reg, value, &offset) || fd_ < 0 ||
        cancelled() || !sms_voltage_is_5v())
        return false;
    if (::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) !=
        static_cast<off_t>(offset))
        return false;
    ssize_t n;
    do { n = ::write(fd_, &value, 1); } while (n < 0 && errno == EINTR);
    return n == 1;
}

bool SmsMdPosixBus::begin_sega_banked()
{
    if (access_ != SmsMdRomRead || !approved() ||
        !sms_mapper_experiment_enabled() ||
        !sms_kernel_mapper_gate_enabled() || cancelled())
        return false;

    // The generated Sanni stream_rom() calls begin before its first read.
    // Only this mapper path needs O_RDWR; probes still open O_RDONLY.
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    fd_ = ::open(device_path(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd_ < 0) return false;

    // Original Sanni cartOn(): disable SRAM and map first three banks.
    // On partial failure the generated wrapper invokes end_sega_banked().
    mapper_started_ = true;
    return mapper_write(0xfffc, 0) &&
           mapper_write(0xfffd, 0) &&
           mapper_write(0xfffe, 1) &&
           mapper_write(0xffff, 2);
}
bool SmsMdPosixBus::select_page2(std::uint8_t bank)
{
    return mapper_started_ && sms_mapper_experiment_enabled() &&
           sms_kernel_mapper_gate_enabled() &&
           mapper_write(0xffff, bank);
}
bool SmsMdPosixBus::end_sega_banked()
{
    if (!mapper_started_) return false;
    // Leave page 2 mapped to the initial bank 2, as in Sanni cartOn().
    const bool ok = mapper_write(0xffff, 2);
    mapper_started_ = false;
    return ok;
}

} } // retrode3::sms