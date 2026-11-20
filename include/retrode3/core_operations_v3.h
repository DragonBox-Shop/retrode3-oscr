#ifndef RETRODE3_CORE_OPERATIONS_V3_H
#define RETRODE3_CORE_OPERATIONS_V3_H

#include "retrode3/core_operations_v1.h"

/* Optional self-describing ABI. Upstream OSCR is not modified.
 * All callbacks operate under the host bus lock except capabilities_json,
 * which MUST be metadata-only and free of cartridge I/O.
 * No JSON parser, STL instance, allocation, or file descriptor crosses ABI. */
#define RETRODE3_CORE_OPERATION_ABI_V3 3u
#define RETRODE3_MAX_PARAMETERS_V3 32u
#define RETRODE3_MAX_PREFIX_V3 64u
#define RETRODE3_MAX_DETAILS_V3 4096u

#ifdef __cplusplus
extern "C" {
#endif

enum retrode3_selection_mode_v3 {
    RETRODE3_SELECT_AUTO_V3 = 0,
    RETRODE3_SELECT_CANDIDATE_V3 = 1,
    RETRODE3_SELECT_LAYOUT_V3 = 2,
    RETRODE3_SELECT_OVERRIDE_V3 = 3
};
enum retrode3_parameter_type_v3 {
    RETRODE3_PARAMETER_INTEGER_V3 = 1,
    RETRODE3_PARAMETER_BOOLEAN_V3 = 2,
    RETRODE3_PARAMETER_STRING_V3 = 3
};
typedef struct retrode3_parameter_v3 {
    char id[48];
    uint32_t type;                  /* integer, boolean, or string/enum */
    uint32_t integer_value;
    char string_value[128];
} retrode3_parameter_v3;

typedef struct retrode3_selection_v3 {
    uint32_t struct_size;
    uint32_t protocol_version;      /* 1; NOT the operation ABI version */
    uint32_t mode;                  /* one of RETRODE3_SELECT_* */
    char schema_id[64];             /* exact core-provided schema revision */
    char candidate_id[64];
    char layout_id[64];
    uint32_t parameter_count;
    retrode3_parameter_v3 parameters[RETRODE3_MAX_PARAMETERS_V3];
} retrode3_selection_v3;

typedef struct retrode3_read_plan_v3 {
    uint32_t struct_size;
    uint64_t payload_bytes;         /* streamed by read_rom, excluding prefix */
    uint32_t prefix_bytes;          /* 0..RETRODE3_MAX_PREFIX_V3 */
} retrode3_read_plan_v3;

typedef struct retrode3_read_result_v3 {
    uint32_t struct_size;
    uint32_t prefix_bytes;
    uint8_t prefix[RETRODE3_MAX_PREFIX_V3];
    uint8_t payload_crc_known;
    uint32_t payload_crc32;
    /* UTF-8 JSON object with read_configuration, identity, verification.
     * verification.status: verified | mismatch | not_found | ambiguous |
     * not_checked. A successful read is never proof of DB verification. */
    char details_json[RETRODE3_MAX_DETAILS_V3];
} retrode3_read_result_v3;

typedef int (*retrode3_core_json_v3_fn)(
    char *json, size_t capacity, char *error, size_t error_capacity);
typedef int (*retrode3_core_plan_v3_fn)(
    const retrode3_selection_v3 *, retrode3_read_plan_v3 *,
    char *error, size_t error_capacity);
typedef int (*retrode3_core_read_rom_v3_fn)(
    const retrode3_selection_v3 *, const retrode3_host_io_v1 *,
    retrode3_read_result_v3 *, char *error, size_t error_capacity);

typedef struct retrode3_core_operations_v3 {
    uint32_t abi_version;
    uint32_t struct_size;
    retrode3_core_json_v3_fn capabilities_json; /* schema v1 object */
    retrode3_core_json_v3_fn fast_probe_json;   /* probe v1 object */
    retrode3_core_plan_v3_fn plan_read;         /* authoritative validation */
    retrode3_core_read_rom_v3_fn read_rom;      /* revalidate & stream payload */
} retrode3_core_operations_v3;
typedef const retrode3_core_operations_v3 *
    (*retrode3_core_operations_v3_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
