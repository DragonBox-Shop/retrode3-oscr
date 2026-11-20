#ifndef RETRODE3_CORE_OPERATIONS_V2_H
#define RETRODE3_CORE_OPERATIONS_V2_H
#include "retrode3/core_operations_v1.h"

/* Optional, additive typed ABI. The v1 symbol remains mandatory. No
 * STL objects, JSON, or allocated memory crosses the DSO boundary. */
#define RETRODE3_CORE_OPERATION_ABI_V2 2u
#define RETRODE3_MAX_CANDIDATES_V2 64u
#define RETRODE3_MAX_LAYOUTS_V2 8u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct retrode3_candidate_v2 {
    char id[32];                   /* stable within pinned catalogue revision */
    char title[128];
    char layout_id[32];
    uint32_t mapper, submapper, prg_kib, chr_kib;
    uint32_t payload_crc32;
    uint8_t read_supported;
} retrode3_candidate_v2;

typedef struct retrode3_layout_v2 {
    char id[32];
    uint32_t mapper, submapper, prg_kib, chr_kib;
    uint8_t read_supported;
} retrode3_layout_v2;

typedef struct retrode3_probe_v2 {
    uint32_t struct_size;
    char identification_status[16]; /* unique, ambiguous, unknown */
    uint32_t candidate_count;
    uint32_t layout_count;
    retrode3_candidate_v2 candidates[RETRODE3_MAX_CANDIDATES_V2];
    retrode3_layout_v2 layouts[RETRODE3_MAX_LAYOUTS_V2];
} retrode3_probe_v2;

typedef struct retrode3_read_request_v2 {
    uint32_t struct_size;
    char candidate_id[32];        /* optional when one safe layout */
    char layout_id[32];           /* optional explicit layout */
    uint8_t manual;               /* all following values are required */
    uint8_t mirroring;            /* 0 horizontal, 1 vertical, 2 four-screen */
    uint8_t battery;              /* boolean */
    uint32_t mapper, submapper, prg_kib, chr_kib;
} retrode3_read_request_v2;

typedef struct retrode3_read_result_v2 {
    uint32_t struct_size;
    uint8_t header[16];           /* final file header, after payload CRC */
    uint8_t verified;             /* read success and DB verification differ */
    uint32_t payload_crc32;
    char matched_candidate_id[32];
    char title[128];
    char layout_id[32];
} retrode3_read_result_v2;

/* v2 streams NES payload only. Host reserves and replaces the initial
 * 16-byte header in its private temporary file before atomic publication.
 * Other cores may implement their own v2 protocol in future revisions. */
typedef int (*retrode3_core_probe_v2_fn)(retrode3_probe_v2 *, char *, size_t);
typedef int (*retrode3_core_read_rom_v2_fn)(
    const retrode3_probe_v2 *, const retrode3_read_request_v2 *,
    const retrode3_host_io_v1 *, retrode3_read_result_v2 *,
    char *, size_t);

typedef struct retrode3_core_operations_v2 {
    uint32_t abi_version;
    uint32_t struct_size;
    retrode3_core_probe_v2_fn fast_probe;
    retrode3_core_read_rom_v2_fn read_rom;
} retrode3_core_operations_v2;

typedef const retrode3_core_operations_v2 *
    (*retrode3_core_operations_v2_fn)(void);

#ifdef __cplusplus
}
#endif
#endif /* RETRODE3_CORE_OPERATIONS_V2_H */
