/* Retrode2 GBx-on-MegaDrive adapter: portable, strictly ROM-only stage 1.
 * ROM/header conventions follow the pinned OSCR GameBoy.cpp and
 * GameBoyAdvance.cpp.  Board-specific wiring never leaks into this file.
 * Not connected to a physical backend until bus direction/voltage tests. */
#include "gbx/retrode2_readonly.h"
#include <string.h>

namespace OSCR { namespace Cores { namespace Retrode2GBx {
namespace {

bool abort_requested(const Bus &bus)
{
    return bus.cancelled && bus.cancelled(bus.context) != 0;
}

void copy_printable(char *out, size_t cap, const uint8_t *src, size_t count)
{
    size_t i = 0;
    while (i < count && i + 1 < cap && src[i] != 0) {
        const uint8_t c = src[i];
        out[i] = (c >= 0x20 && c <= 0x7e) ? static_cast<char>(c) : '_';
        ++i;
    }
    while (i > 0 && out[i - 1] == ' ') --i;
    out[i] = 0;
}

bool same_gb(const GbHeader &a, const GbHeader &b)
{
    return a.cart_type == b.cart_type &&
           a.rom_size_code == b.rom_size_code &&
           a.ram_size_code == b.ram_size_code &&
           a.header_checksum == b.header_checksum &&
           strcmp(a.title, b.title) == 0;
}

bool same_gba(const GbaHeader &a, const GbaHeader &b)
{
    return a.header_checksum == b.header_checksum &&
           strcmp(a.title, b.title) == 0 &&
           strcmp(a.game_code, b.game_code) == 0;
}

Status send(const Sink &sink, const uint8_t *data, size_t size)
{
    return sink.write(sink.context, data, size) == 0 ? Ok : OutputFailed;
}

} // namespace

Status probe_gb(const Bus &bus, GbHeader *header)
{
    if (!header || !bus.gb_read8) return BadArgument;
    uint8_t bytes[0x50] = {};
    for (unsigned i = 0; i < sizeof(bytes); ++i) {
        if (abort_requested(bus)) return Cancelled;
        if (bus.gb_read8(bus.context, static_cast<uint16_t>(0x100u + i),
                         &bytes[i]) != 0) return ReadFailed;
    }
    // OSCR GameBoy::checkCart() subtracts every header byte and one.
    uint8_t check = 0;
    for (unsigned address = 0x134u; address < 0x14du; ++address)
        check = static_cast<uint8_t>(check - bytes[address - 0x100u] - 1u);
    if (check != bytes[0x4d]) return InvalidHeader;

    GbHeader result = {};
    copy_printable(result.title, sizeof(result.title), &bytes[0x34], 16);
    result.cart_type = bytes[0x47];
    result.rom_size_code = bytes[0x48];
    result.ram_size_code = bytes[0x49];
    result.header_checksum = bytes[0x4d];
    // The only ROM layout requiring NO mapper-control writes.
    result.linear_32k_supported = result.rom_size_code == 0 &&
        (result.cart_type == 0x00 || result.cart_type == 0x08 ||
         result.cart_type == 0x09);
    *header = result;
    return Ok;
}

Status probe_gba(const Bus &bus, GbaHeader *header)
{
    if (!header || !bus.gba_read16) return BadArgument;
    uint8_t bytes[0xc0] = {};
    for (unsigned i = 0; i < sizeof(bytes); i += 2) {
        if (abort_requested(bus)) return Cancelled;
        uint16_t word = 0;
        if (bus.gba_read16(bus.context, i, &word) != 0) return ReadFailed;
        bytes[i] = static_cast<uint8_t>(word);
        bytes[i + 1] = static_cast<uint8_t>(word >> 8);
    }
    // OSCR GameBoyAdvance::getCartInfo() uses this logo-byte sum.
    unsigned sum = 0;
    for (unsigned i = 0x04; i < 0xa0; ++i) sum += bytes[i];
    if (sum != 0x4b1bu) return InvalidHeader;
    uint8_t check = 0;
    for (unsigned i = 0xa0; i < 0xbd; ++i)
        check = static_cast<uint8_t>(check - bytes[i]);
    check = static_cast<uint8_t>(check - 0x19u);
    if (check != bytes[0xbd]) return InvalidHeader;

    GbaHeader result = {};
    copy_printable(result.title, sizeof(result.title), &bytes[0xa0], 12);
    copy_printable(result.game_code, sizeof(result.game_code), &bytes[0xac], 4);
    result.header_checksum = bytes[0xbd];
    *header = result;
    return Ok;
}

Status read_gb_linear_32k(const Bus &bus, const Sink &sink,
                          const GbHeader *expected)
{
    if (!sink.write || !bus.gb_read8) return BadArgument;
    GbHeader current = {};
    const Status probed = probe_gb(bus, &current);
    if (probed != Ok) return probed;
    if (!current.linear_32k_supported) return UnsupportedLayout;
    if (expected && !same_gb(*expected, current)) return CartridgeChanged;

    uint8_t block[512];
    for (unsigned pos = 0; pos < 32768u; pos += sizeof(block)) {
        for (unsigned i = 0; i < sizeof(block); ++i) {
            if (abort_requested(bus)) return Cancelled;
            if (bus.gb_read8(bus.context, static_cast<uint16_t>(pos + i),
                             &block[i]) != 0) return ReadFailed;
        }
        const Status wrote = send(sink, block, sizeof(block));
        if (wrote != Ok) return wrote;
    }
    return Ok;
}

Status read_gba_linear(const Bus &bus, const Sink &sink,
                       const GbaHeader *expected, uint32_t size_bytes)
{
    if (!sink.write || !bus.gba_read16) return BadArgument;
    // Exact size set from Sanni's GBA menu. Never guess beyond cart header.
    if (size_bytes != 1u * 1024u * 1024u &&
        size_bytes != 2u * 1024u * 1024u &&
        size_bytes != 4u * 1024u * 1024u &&
        size_bytes != 8u * 1024u * 1024u &&
        size_bytes != 16u * 1024u * 1024u &&
        size_bytes != 32u * 1024u * 1024u) return UnsupportedLayout;
    GbaHeader current = {};
    const Status probed = probe_gba(bus, &current);
    if (probed != Ok) return probed;
    if (expected && !same_gba(*expected, current)) return CartridgeChanged;

    uint8_t block[512];
    for (uint32_t pos = 0; pos < size_bytes; pos += sizeof(block)) {
        for (unsigned i = 0; i < sizeof(block); i += 2) {
            if (abort_requested(bus)) return Cancelled;
            uint16_t word = 0;
            if (bus.gba_read16(bus.context, pos + i, &word) != 0)
                return ReadFailed;
            block[i] = static_cast<uint8_t>(word);
            block[i + 1] = static_cast<uint8_t>(word >> 8);
        }
        const Status wrote = send(sink, block, sizeof(block));
        if (wrote != Ok) return wrote;
    }
    return Ok;
}

}}} // namespace OSCR::Cores::Retrode2GBx
