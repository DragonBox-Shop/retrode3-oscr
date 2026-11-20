#ifndef RETRODE3_SMS_ROM_CORE_V1_H
#define RETRODE3_SMS_ROM_CORE_V1_H

// Hardware-agnostic adaptation of the SMS/Retrode branch in Sanni's
// Cart_Reader/SMS.ino. This is NOT a raw GPIO or automatic write interface.
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

// An implementation is responsible for approved electrical control,
// serialization, stable power and register-write safety. No platform
// implementation is shipped for the unverified Retrode2 adapter.
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

// Mirrors the tested logical loop structure in Sanni's readROM_SMS():
// <=32 KiB uses CPU 0..size-1, larger ROMs select 16 KiB banks in page 2
// ($8000..$BFFF). Unlike Sanni's Arduino code, all mapper operations
// remain behind the reviewed Bus interface and are only exercised in mocks.
inline void stream_rom(Bus &bus, Layout layout, void *context, WriteFn write)
{
    if (!valid_layout(layout)) throw std::runtime_error("unsupported SMS layout");
    if (!write) throw std::runtime_error("SMS output callback missing");
    if (!bus.approved()) throw std::runtime_error("SMS adapter bus unapproved");
    const std::uint32_t length = layout.rom_kib * 1024u;
    const bool banked = layout.mapping == Sega16k;
    bool initialized = false;
    try {
        if (banked) {
            if (!bus.begin_sega_banked()) {
                // A partial failed setup may already have touched registers.
                (void)bus.end_sega_banked();
                throw std::runtime_error("SMS mapper init failed");
            }
            initialized = true;
        }
        std::uint8_t bytes[512];
        const std::uint32_t window_size = banked ? 16384u : length;
        for (std::uint32_t page = 0; page < length / window_size; ++page) {
            if (banked && !bus.select_page2(static_cast<std::uint8_t>(page)))
                throw std::runtime_error("SMS bank select failed");
            for (std::uint32_t offset = 0; offset < window_size; offset += sizeof(bytes)) {
                if (bus.cancelled()) throw std::runtime_error("cancelled");
                const std::uint16_t address = static_cast<std::uint16_t>(
                    (banked ? 0x8000u : 0u) + offset);
                if (!bus.read(address, bytes, sizeof(bytes)))
                    throw std::runtime_error("SMS ROM bus read failed");
                if (write(context, bytes, sizeof(bytes)))
                    throw std::runtime_error("SMS host output rejected data");
            }
        }
    } catch (...) {
        if (initialized) (void)bus.end_sega_banked();
        throw;
    }
    if (initialized && !bus.end_sega_banked())
        throw std::runtime_error("SMS mapper cleanup failed");
}

} } // namespace retrode3::sms
#endif
