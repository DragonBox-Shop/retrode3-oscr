// Experimental Flash transport for the existing MegaDrive .so core.
// The common CLI (not this adapter) owns source files, NDJSON, progress,
// cancellation, exact verification and the shared bus lock.
// This file is linked ONLY by the opt-in native-md-flash-lab target.
#include "retrode3/core_flash_v1.h"
#include "retrode3/flash_kernel_test_v1.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
const char *const SLOT_DEVICE = "/dev/slot-md";
const uint32_t FLASH_SIZE = 4194304u;

int fail(char *err, size_t cap, const char *msg) {
    if (err && cap)
        std::snprintf(err, cap, "%s", msg);
    return -1;
}
int sysfail(char *err, size_t cap, const char *msg) {
    if (err && cap)
        std::snprintf(err, cap, "%s (errno %d)", msg, errno);
    return -1;
}
int open_slot(char *err, size_t cap) {
    int fd = ::open(SLOT_DEVICE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return sysfail(err, cap, "Cannot open Flash MD slot");
    return fd;
}

/* All calls in a Flash job belong to a single CLI worker. Keep the slot FD
 * open from explicit Identify through Erase/Write/Verify: the kernel ties
 * the one-time CFI authorization to this FD and revokes it on cart removal.
 * A new Identify starts a fresh session; process exit closes it automatically. */
struct FlashSlotSession {
    int fd;
    FlashSlotSession() : fd(-1) {}
    ~FlashSlotSession() { reset(); }
    void reset() {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
    int begin(char *err, size_t cap) {
        reset();
        fd = open_slot(err, cap);
        return fd;
    }
};
FlashSlotSession session;

int identify(retrode3_flash_identity_v1 *identity,
             char *err, size_t cap) {
    if (!identity || identity->struct_size != sizeof(*identity))
        return fail(err, cap, "Invalid Flash identity ABI");
    retrode3_flash_id_v1 id;
    std::memset(&id, 0, sizeof(id));
    id.abi_version = RETRODE3_FLASH_CFI_ABI_V1;
    int fd = session.begin(err, cap);
    if (fd < 0) return -1;
    int rc = ::ioctl(fd, RETRODE3_FLASH_IOC_IDENTIFY, &id);
    const int saved_errno = errno;
    if (rc != 0) {
        session.reset();
        errno = saved_errno;
        return sysfail(err, cap, "Flash identify rejected");
    }
    if (id.capacity_bytes != FLASH_SIZE || id.command_set != 2u ||
        id.sector_count != 71u || id.device_id != 0x22f9u ||
        (id.manufacturer_id != 0x007fu && id.manufacturer_id != 0x001cu)) {
        session.reset();
        return fail(err, cap, "Unknown or incompatible Flash chip");
    }
    identity->manufacturer_id = id.manufacturer_id;
    identity->device_id = id.device_id;
    identity->size_bytes = id.capacity_bytes;
    identity->command_set = id.command_set;
    identity->region_count = 2;
    identity->regions[0].start_bytes = 0;
    identity->regions[0].sector_bytes = 8192;
    identity->regions[0].count = 8;
    identity->regions[1].start_bytes = 65536;
    identity->regions[1].sector_bytes = 65536;
    identity->regions[1].count = 63;
    return 0;
}
int erase_sector(uint32_t offset, char *err, size_t cap) {
    retrode3_flash_erase_v1 job;
    std::memset(&job, 0, sizeof(job));
    job.abi_version = RETRODE3_FLASH_CFI_ABI_V1;
    job.offset_bytes = offset;
    int fd = session.fd;
    if (fd < 0)
        return fail(err, cap, "Flash session is not identified; run Detect Flash first");
    int rc = ::ioctl(fd, RETRODE3_FLASH_IOC_ERASE, &job);
    int saved_errno = errno;
    if (rc != 0) {
        errno = saved_errno;
        return sysfail(err, cap, "Flash sector erase rejected");
    }
    return 0;
}
int program(uint32_t addr, const void *data, size_t count,
            char *err, size_t cap) {
    if (!data || !count || count > RETRODE3_FLASH_CHUNK_BYTES ||
        (count & 1u) || (addr & 1u) ||
        addr > FLASH_SIZE - count)
        return fail(err, cap, "Invalid Flash programming chunk");
    retrode3_flash_program_v1 job;
    std::memset(&job, 0, sizeof(job));
    job.abi_version = RETRODE3_FLASH_CFI_ABI_V1;
    job.offset_bytes = addr;
    job.length = static_cast<__u32>(count);
    std::memcpy(job.data, data, count);
    int fd = session.fd;
    if (fd < 0)
        return fail(err, cap, "Flash session is not identified; run Detect Flash first");
    int rc = ::ioctl(fd, RETRODE3_FLASH_IOC_PROGRAM, &job);
    int saved_errno = errno;
    if (rc != 0) {
        errno = saved_errno;
        return sysfail(err, cap, "Flash chunk program rejected");
    }
    return 0;
}
int readback(uint32_t addr, void *data, size_t count,
             char *err, size_t cap) {
    if (!data || !count || count > 4096u ||
        addr >= FLASH_SIZE || count > FLASH_SIZE - addr)
        return fail(err, cap, "Invalid Flash readback length");
    int fd = session.fd;
    if (fd < 0)
        return fail(err, cap, "Flash session is not identified; run Detect Flash first");
    if (::lseek(fd, static_cast<off_t>(addr), SEEK_SET) !=
        static_cast<off_t>(addr)) {
        int saved_errno = errno;
        errno = saved_errno;
        return sysfail(err, cap, "Cannot seek MD Flash");
    }
    size_t done = 0;
    while (done < count) {
        ssize_t n = ::read(fd, static_cast<unsigned char *>(data) + done,
                           count - done);
        if (n <= 0) {
            int saved_errno = errno;
            errno = saved_errno;
            return sysfail(err, cap, "Incomplete Flash readback");
        }
        done += static_cast<size_t>(n);
    }
    return 0;
}
} // namespace

extern "C" const retrode3_core_flash_operations_v1 *
retrode3_core_get_flash_operations_v1(void) {
    static const retrode3_core_flash_operations_v1 methods = {
        RETRODE3_CORE_FLASH_ABI_V1,
        sizeof(retrode3_core_flash_operations_v1),
        identify, erase_sector, program, readback
    };
    return &methods;
}
