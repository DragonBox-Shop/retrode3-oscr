#ifndef RETRODE3_CORE_FLASH_V1_H
#define RETRODE3_CORE_FLASH_V1_H

/* Optional experimental Flash ABI. Linked dynamically into an individual
 * core; shared retrode3-cli owns NDJSON, source files, progress and verify.
 * No filesystem paths, STL objects, exceptions or ioctls cross this ABI.
 * The regular v1/v2/v3 ROM-reading core contracts are not changed. */
#include <stddef.h>
#include <stdint.h>

#define RETRODE3_CORE_FLASH_ABI_V1 1u
#define RETRODE3_FLASH_MAX_REGIONS_V1 8u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct retrode3_flash_region_v1 {
    uint32_t start_bytes;
    uint32_t sector_bytes;
    uint32_t count;
} retrode3_flash_region_v1;

typedef struct retrode3_flash_identity_v1 {
    uint32_t struct_size;
    uint32_t manufacturer_id;
    uint32_t device_id;
    uint32_t size_bytes;
    uint32_t command_set;
    uint32_t region_count;
    retrode3_flash_region_v1 regions[RETRODE3_FLASH_MAX_REGIONS_V1];
} retrode3_flash_identity_v1;

typedef int (*retrode3_flash_identify_v1_fn)(
    retrode3_flash_identity_v1 *, char *error, size_t error_capacity);
typedef int (*retrode3_flash_erase_v1_fn)(
    uint32_t aligned_sector, char *error, size_t error_capacity);
typedef int (*retrode3_flash_program_v1_fn)(
    uint32_t offset_bytes, const void *bytes, size_t count,
    char *error, size_t error_capacity);
typedef int (*retrode3_flash_read_v1_fn)(
    uint32_t offset_bytes, void *bytes, size_t count,
    char *error, size_t error_capacity);

typedef struct retrode3_core_flash_operations_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    retrode3_flash_identify_v1_fn identify;
    retrode3_flash_erase_v1_fn erase_sector;
    retrode3_flash_program_v1_fn program;
    retrode3_flash_read_v1_fn readback;
} retrode3_core_flash_operations_v1;

typedef const retrode3_core_flash_operations_v1 *
    (*retrode3_core_get_flash_operations_v1_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
