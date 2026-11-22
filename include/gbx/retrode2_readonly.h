#ifndef OSCR_RETRODE2_GBX_READONLY_H
#define OSCR_RETRODE2_GBX_READONLY_H

#include <stddef.h>
#include <stdint.h>

/* No OSCR, AVR, Linux or Retrode3 ABI dependencies: the exact same reader
 * can be linked to an OSCR MegaDrive-slot backend and to Retrode3's HAL.
 * The transport owns voltage, direction changes, control-signal timing and
 * cart removal.  NO RAM, flash or mapper-write operation is exposed here. */
namespace OSCR { namespace Cores { namespace Retrode2GBx {

enum Status {
    Ok = 0,
    BadArgument = -1,
    ReadFailed = -2,
    InvalidHeader = -3,
    UnsupportedLayout = -4,
    Cancelled = -5,
    OutputFailed = -6,
    CartridgeChanged = -7
};

struct Bus {
    void *context;
    /* GB/GBC: logical address 0x0000..0x7fff, 8-bit result. */
    int (*gb_read8)(void *context, uint16_t address, uint8_t *value);
    /* GBA: byte address, even only; returns a host-endian 16-bit word.
     * The transport performs the address-latch/direction/RD cycle. */
    int (*gba_read16)(void *context, uint32_t byte_address, uint16_t *value);
    int (*cancelled)(void *context);
};

struct Sink {
    void *context;
    /* Return zero if the complete chunk was accepted, nonzero on failure. */
    int (*write)(void *context, const uint8_t *bytes, size_t count);
};

struct GbHeader {
    char title[17];
    uint8_t cart_type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    uint8_t header_checksum;
    bool linear_32k_supported;
};

struct GbaHeader {
    char title[13];
    char game_code[5];
    uint8_t header_checksum;
};

/* Hardware voltage selection MUST happen before probe, outside this API:
 * GB/GBC: 5 V, GBA: 3.3 V; neither may be tried speculatively. */
Status probe_gb(const Bus &bus, GbHeader *header);
Status probe_gba(const Bus &bus, GbaHeader *header);

/* First ROM-only milestone. GB supports only no-MBC 32 KiB; GBA requires
 * explicit ROM size since its header does not describe physical ROM size.
 * Reprobe before streaming; do not assume a previous cart is still there.
 * These functions do NOT perform database verification or save operations. */
Status read_gb_linear_32k(const Bus &bus, const Sink &sink,
                          const GbHeader *expected);
Status read_gba_linear(const Bus &bus, const Sink &sink,
                       const GbaHeader *expected, uint32_t size_bytes);

}}} // namespace OSCR::Cores::Retrode2GBx
#endif
