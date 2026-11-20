#ifndef RETRODE3_SMS_ROM_CORE_V1_H
#define RETRODE3_SMS_ROM_CORE_V1_H

// Thin core API around ORIGINAL pinned OSCR src/cores/SMS.cpp algorithms.
// Build-generated headers supply ROM-size and bank-reading loops, not this file.
#include "retrode3/sms_header_v1.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace retrode3 { namespace sms {

enum Mapping { Linear, Sega16k };
struct Layout {
    Mapping mapping;
    std::uint32_t rom_kib;
};
struct Probe {
    bool header_found;
    bool conflicting_headers;
    std::uint16_t address;
    Header header;
};

// Each platform implementation owns electrical control, serialization,
// power checks and register-write safety. Current POSIX bus is experimental
// and deliberately refuses all physical bank/mapper register writes.
struct Bus {
    virtual ~Bus() {}
    virtual bool approved() const = 0;
    virtual bool read(std::uint16_t cpu_address, std::uint8_t *dst,
                      std::size_t count) = 0;
    virtual bool begin_sega_banked() = 0;  // reviewed $FFFC..$FFFF init
    virtual bool select_page2(std::uint8_t bank) = 0; // reviewed $FFFF write
    virtual bool end_sega_banked() = 0;    // restore mapper to safe state
    virtual bool cancelled() const = 0;
};

typedef int (*WriteFn)(void *context, const void *bytes, std::size_t count);

inline bool valid_layout(Layout layout)
{
    if (layout.mapping == Linear)
        return layout.rom_kib == 8u || layout.rom_kib == 16u ||
               layout.rom_kib == 32u;
    if (layout.mapping == Sega16k)
        return layout.rom_kib == 48u || layout.rom_kib == 64u ||
               layout.rom_kib == 128u || layout.rom_kib == 256u ||
               layout.rom_kib == 512u || layout.rom_kib == 1024u;
    return false;
}

// Only three 16-byte reads. No writes, mapper initialization or mirror
// scanning. Header metadata is a size HINT, never game/ROM verification.
inline Probe bounded_probe(Bus &bus)
{
    if (!bus.approved()) throw std::runtime_error("SMS adapter bus unapproved");
    Probe probe = Probe();
    const std::uint16_t addresses[] = {0x1ff0u, 0x3ff0u, 0x7ff0u};
    for (unsigned i = 0; i < 3u; ++i) {
        if (bus.cancelled()) throw std::runtime_error("cancelled");
        std::uint8_t window[16] = {};
        if (!bus.read(addresses[i], window, sizeof(window)))
            throw std::runtime_error("SMS bounded header read failed");
        Header header = Header();
        if (!decode_header(window, sizeof(window), &header)) continue;
        if (!probe.header_found) {
            probe.header_found = true;
            probe.header = header;
            probe.address = addresses[i];
        } else if (header.declared_rom_bytes != probe.header.declared_rom_bytes ||
                   header.checksum != probe.header.checksum ||
                   header.region_code != probe.header.region_code) {
            probe.conflicting_headers = true;
        }
    }
    return probe;
}

// Generated from the actual pinned OSCR::Cores::SMS::readROM bank and
// buffer loops. This header must be regenerated, not hand-edited.
#include "oscr_sms_read.h"

} } // namespace retrode3::sms
#endif