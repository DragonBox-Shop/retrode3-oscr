#ifndef RETRODE3_CORE_DESCRIPTOR_V1_H
#define RETRODE3_CORE_DESCRIPTOR_V1_H

#include <stdint.h>

/*
 * Stable discovery ABI for optional, independently packaged Retrode3
 * cartridge cores. This is metadata ONLY, not an operation dispatch ABI.
 * Publishing a descriptor never implies that a cartridge operation is safe.
 *
 * The core library must export the symbol:
 *   const retrode3_core_descriptor_v1 *retrode3_core_describe_v1(void);
 *
 * The original OSCR upstream core must not be changed to implement this:
 * a Retrode3-owned adapter shared library exports the descriptor instead.
 */
#define RETRODE3_CORE_DISCOVERY_ABI_V1 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct retrode3_core_descriptor_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *id;       /* stable ASCII slug, must match <id>.so */
    const char *name;     /* human-readable, UTF-8 */
    const char *version;  /* package/core version */
    const char *slot;     /* required physical slot: md, nes, snes, ... */
} retrode3_core_descriptor_v1;

typedef const retrode3_core_descriptor_v1 *(*retrode3_core_describe_v1_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
