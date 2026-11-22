// Installed only in mock CI/test directories: no cartridge hardware access.
#include "retrode3/core_descriptor_v1.h"

#ifndef RETRODE3_MOCK_ID
#define RETRODE3_MOCK_ID "snes"
#endif
#ifndef RETRODE3_MOCK_NAME
#define RETRODE3_MOCK_NAME "Super Nintendo"
#endif
#ifndef RETRODE3_MOCK_SLOT
#define RETRODE3_MOCK_SLOT "snes"
#endif
#ifndef RETRODE3_MOCK_ABI
#define RETRODE3_MOCK_ABI RETRODE3_CORE_DISCOVERY_ABI_V1
#endif

extern "C" const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void)
{
    static const retrode3_core_descriptor_v1 descriptor = {
        RETRODE3_MOCK_ABI,
        sizeof(retrode3_core_descriptor_v1),
        RETRODE3_MOCK_ID,
        RETRODE3_MOCK_NAME,
        "0.1.0-test",
        RETRODE3_MOCK_SLOT
    };
    return &descriptor;
}


#ifdef RETRODE3_MOCK_OPERATIONS
#include "retrode3/core_operations_v1.h"
#include <cstring>

namespace {
int probe(retrode3_probe_v1 *p, char *error, size_t error_capacity)
{
    if (!p || p->struct_size < sizeof(*p)) {
        if (error && error_capacity) error[0] = '\0';
        return -1;
    }
    p->rom_size_bytes = 131072;
    std::strcpy(p->title, "MOCK SNES");
    std::strcpy(p->mapping, "lo");
    std::strcpy(p->checksum, "1234");
    return 0;
}
int read_rom(const retrode3_probe_v1 *p, const retrode3_host_io_v1 *io,
             char *error, size_t error_capacity)
{
    if (!p || !io || io->abi_version != RETRODE3_CORE_OPERATION_ABI_V1 ||
        io->struct_size < sizeof(*io) || !io->write || !io->cancelled) {
        if (error && error_capacity) std::strncpy(error, "invalid host IO", error_capacity - 1);
        return -1;
    }
    unsigned char buffer[1024];
    for (uint64_t offset = 0; offset < p->rom_size_bytes; offset += sizeof(buffer)) {
        if (io->cancelled(io->context)) return -1;
        for (size_t i = 0; i < sizeof(buffer); ++i)
            buffer[i] = static_cast<unsigned char>(((offset + i) * 7u) & 255);
        if (io->write(io->context, buffer, sizeof(buffer))) return -1;
    }
    return 0;
}
void release() {}
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
#endif

#ifdef RETRODE3_MOCK_FLASH_OPERATIONS
// Explicit test-only plugin implementing the additive Flash ABI.
// There is NO bus access and the fixture deliberately cannot program.
#include "retrode3/core_flash_v1.h"
#include <cstring>
namespace {
int mock_flash_capabilities(char *json, size_t capacity, char *, size_t) {
    static const char value[] =
        "{\\\"version\\\":1,\\\"cfi_supported\\\":true,\\\"probe_supported\\\":true,"
        "\\\"write_supported\\\":false,\\\"verify_supported\\\":false}";
    if (!json || capacity < sizeof(value)) return -1;
    std::memcpy(json, value, sizeof(value));
    return 0;
}
int mock_flash_probe(retrode3_flash_probe_v1 *probe, char *, size_t) {
    if (!probe || probe->struct_size != sizeof(*probe)) return -1;
    probe->cfi_valid = 1;
    probe->write_supported = 0;
    probe->verify_supported = 0;
    probe->manufacturer_id = 0x7f;
    probe->device_id = 0x1234;
    probe->primary_command_set = 2;
    probe->interface_code = 2;
    probe->capacity_bytes = 4194304u;
    probe->region_count = 1;
    probe->regions[0].start_bytes = 0;
    probe->regions[0].block_bytes = 65536;
    probe->regions[0].blocks = 64;
    std::strcpy(probe->adapter_profile, "offline-simulated");
    std::strcpy(probe->diagnostic, "mock fixture - no hardware access");
    return 0;
}
int mock_flash_plan(const retrode3_flash_probe_v1 *, uint64_t,
                    char *, size_t) { return -1; }
int mock_flash_write(const retrode3_flash_probe_v1 *, uint64_t,
                     const retrode3_flash_host_io_v1 *,
                     char *, size_t) { return -1; }
} // anonymous namespace
extern "C" const retrode3_core_flash_operations_v1 *
retrode3_core_get_flash_operations_v1(void)
{
    static const retrode3_core_flash_operations_v1 functions = {
        RETRODE3_CORE_FLASH_ABI_V1, sizeof(retrode3_core_flash_operations_v1),
        mock_flash_capabilities, mock_flash_probe, mock_flash_plan, mock_flash_write
    };
    return &functions;
}
#endif
