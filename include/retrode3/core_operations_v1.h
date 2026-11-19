#ifndef RETRODE3_CORE_OPERATIONS_V1_H
#define RETRODE3_CORE_OPERATIONS_V1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Versioned C ABI between the single Retrode3 host and separately installed
 * cartridge-core adapters. The ABI belongs to Retrode3, not upstream OSCR.
 * Core adapters MUST catch their own exceptions; no exceptions, file handles,
 * JSON, allocated strings or C++ runtime objects cross this interface.
 */
#define RETRODE3_CORE_OPERATION_ABI_V1 1u

typedef struct retrode3_probe_v1 {
    uint32_t struct_size;           /* host initializes to sizeof(struct) */
    uint64_t rom_size_bytes;        /* 0 when unavailable */
    char title[128];                /* NUL-terminated UTF-8 */
    char mapping[16];               /* e.g. lo, hi, linear */
    char checksum[9];               /* hex or empty */
} retrode3_probe_v1;

/* Callback returns 0 if all bytes accepted, nonzero to stop the operation. */
typedef int (*retrode3_core_write_v1_fn)(void *context, const void *bytes, size_t count);
typedef int (*retrode3_core_cancel_v1_fn)(void *context);

typedef struct retrode3_host_io_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *context;
    retrode3_core_write_v1_fn write;
    retrode3_core_cancel_v1_fn cancelled;
} retrode3_host_io_v1;

typedef int (*retrode3_core_probe_v1_fn)(retrode3_probe_v1 *probe,
                                          char *error, size_t error_capacity);
typedef int (*retrode3_core_read_rom_v1_fn)(const retrode3_probe_v1 *probe,
                                             const retrode3_host_io_v1 *io,
                                             char *error, size_t error_capacity);
typedef void (*retrode3_core_release_v1_fn)(void);

typedef struct retrode3_core_operations_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    retrode3_core_probe_v1_fn probe;
    retrode3_core_read_rom_v1_fn read_rom; /* NULL if not implemented */
    retrode3_core_release_v1_fn release;  /* mandatory: close/power-down */
} retrode3_core_operations_v1;

/* Optional exported symbol; absence means descriptor-only, not runnable:
 * const retrode3_core_operations_v1 *retrode3_core_get_operations_v1(void);
 */
typedef const struct retrode3_core_operations_v1
    *(*retrode3_core_operations_v1_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* RETRODE3_CORE_OPERATIONS_V1_H */
