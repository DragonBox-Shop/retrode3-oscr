#ifndef RETRODE3_CORE_FLASH_V1_H
#define RETRODE3_CORE_FLASH_V1_H

// Optional ADDITIVE write-cartridge ABI; not an extension to read ABI v3.
// Everything remains locked behind explicit core selection and a host bus
// lock. The host, never a browser, validates the source and authorizes writes.
// No C++ objects, file descriptors or allocations cross this ABI.
#include <stddef.h>
#include <stdint.h>

#define RETRODE3_CORE_FLASH_ABI_V1 1u
#define RETRODE3_FLASH_MAX_REGIONS_V1 8u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct retrode3_flash_region_v1 {
    uint64_t start_bytes;
    uint32_t block_bytes;
    uint32_t blocks;
} retrode3_flash_region_v1;

typedef struct retrode3_flash_probe_v1 {
    uint32_t struct_size;
    uint16_t manufacturer_id;
    uint16_t device_id;
    uint16_t primary_command_set;
    uint16_t interface_code;
    uint64_t capacity_bytes;
    uint32_t region_count;
    retrode3_flash_region_v1 regions[RETRODE3_FLASH_MAX_REGIONS_V1];
    uint8_t cfi_valid;
    uint8_t write_supported;
    uint8_t verify_supported;
    uint8_t reserved;
    char adapter_profile[48];
    char diagnostic[192];
} retrode3_flash_probe_v1;

// Source data is streamed from an already-validated host-side temporary
// file. Callback returns 0 only for an exact count. No path crosses ABI.
typedef int (*retrode3_flash_source_v1_fn)(void *, void *, size_t);
typedef int (*retrode3_flash_cancel_v1_fn)(void *);
typedef int (*retrode3_flash_progress_v1_fn)(void *, uint64_t, uint64_t);

typedef struct retrode3_flash_host_io_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *context;
    retrode3_flash_source_v1_fn read_source;
    retrode3_flash_cancel_v1_fn cancelled;
    retrode3_flash_progress_v1_fn progress;
} retrode3_flash_host_io_v1;

// Capabilities are read-only metadata. Probe MAY issue flash query commands,
// so it must never be invoked by automatic ROM insertion detection.
typedef int (*retrode3_flash_capabilities_v1_fn)(
    char *, size_t, char *, size_t);
typedef int (*retrode3_flash_probe_fn_v1)(
    retrode3_flash_probe_v1 *, char *, size_t);
typedef int (*retrode3_flash_plan_fn_v1)(
    const retrode3_flash_probe_v1 *, uint64_t, char *, size_t);
// The implementation MUST re-probe and match before the first erase. It
// performs erase, program, and full readback comparison, and reports failures.
typedef int (*retrode3_flash_write_fn_v1)(
    const retrode3_flash_probe_v1 *, uint64_t,
    const retrode3_flash_host_io_v1 *, char *, size_t);

typedef struct retrode3_core_flash_operations_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    retrode3_flash_capabilities_v1_fn capabilities_json;
    retrode3_flash_probe_fn_v1 probe;
    retrode3_flash_plan_fn_v1 plan_write;
    retrode3_flash_write_fn_v1 write_verify;
} retrode3_core_flash_operations_v1;

typedef const retrode3_core_flash_operations_v1 *
    (*retrode3_core_flash_operations_v1_fn)(void);
// Exported optional symbol: retrode3_core_get_flash_operations_v1

#ifdef __cplusplus
}
#endif
#endif
