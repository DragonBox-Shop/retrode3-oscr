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
