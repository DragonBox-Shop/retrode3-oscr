#include "gbx/retrode2_readonly.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <vector>

using namespace OSCR::Cores::Retrode2GBx;

struct Fixture {
    std::vector<uint8_t> gb;
    std::vector<uint8_t> gba;
    std::vector<uint8_t> written;
    unsigned gb_reads;
    unsigned gba_reads;
    unsigned write_calls;
    bool cancellation;

    Fixture() : gb(32768, 0), gba(1024 * 1024, 0), gb_reads(0),
                gba_reads(0), write_calls(0), cancellation(false) {}
};

static int gb_read(void *ctx, uint16_t addr, uint8_t *value)
{
    Fixture &f = *static_cast<Fixture *>(ctx);
    ++f.gb_reads;
    if (addr >= f.gb.size()) return -1;
    *value = f.gb[addr];
    return 0;
}
static int gba_read(void *ctx, uint32_t addr, uint16_t *value)
{
    Fixture &f = *static_cast<Fixture *>(ctx);
    ++f.gba_reads;
    if (addr & 1u || addr + 1u >= f.gba.size()) return -1;
    *value = static_cast<uint16_t>(f.gba[addr] | (f.gba[addr + 1u] << 8));
    return 0;
}
static int cancelled(void *ctx)
{
    return static_cast<Fixture *>(ctx)->cancellation ? 1 : 0;
}
static int write_block(void *ctx, const uint8_t *buffer, size_t count)
{
    Fixture &f = *static_cast<Fixture *>(ctx);
    ++f.write_calls;
    f.written.insert(f.written.end(), buffer, buffer + count);
    return 0;
}
static Bus bus(Fixture &f)
{
    Bus b = {&f, gb_read, gba_read, cancelled};
    return b;
}
static Sink sink(Fixture &f)
{
    Sink s = {&f, write_block};
    return s;
}
static void fix_gb_checksum(Fixture &f)
{
    uint8_t c = 0;
    for (size_t i = 0x134; i < 0x14d; ++i)
        c = static_cast<uint8_t>(c - f.gb[i] - 1u);
    f.gb[0x14d] = c;
}
static void fix_gba_checksum(Fixture &f)
{
    uint8_t c = 0;
    for (size_t i = 0xa0; i < 0xbd; ++i)
        c = static_cast<uint8_t>(c - f.gba[i]);
    f.gba[0xbd] = static_cast<uint8_t>(c - 0x19u);
}

int main()
{
    Fixture f;
    for (size_t i = 0; i < f.gb.size(); ++i)
        f.gb[i] = static_cast<uint8_t>(i * 7u + 3u);
    memcpy(&f.gb[0x134], "GB TEST CARTRIDGE", 16);
    f.gb[0x147] = 0x00; // no MBC
    f.gb[0x148] = 0x00; // 32 KiB
    f.gb[0x149] = 0x00;
    fix_gb_checksum(f);
    GbHeader gh = {};
    assert(probe_gb(bus(f), &gh) == Ok);
    assert(gh.linear_32k_supported && gh.rom_size_code == 0);
    assert(read_gb_linear_32k(bus(f), sink(f), &gh) == Ok);
    assert(f.written == f.gb && f.write_calls == 64);
    assert(f.gb_reads == 0x50u * 2u + 32768u);
    f.written.clear();
    f.gb[0x147] = 1; // MBC1 requires mapper-register writes
    fix_gb_checksum(f);
    assert(read_gb_linear_32k(bus(f), sink(f), 0) == UnsupportedLayout);
    assert(f.written.empty());
    assert(probe_gb(bus(f), &gh) == Ok && !gh.linear_32k_supported);
    f.gb[0x148] = 0;
    f.gb[0x147] = 0;
    fix_gb_checksum(f);
    assert(read_gb_linear_32k(bus(f), sink(f), &gh) == CartridgeChanged);
    assert(f.written.empty());
    f.gb[0x14d] ^= 0x80;
    assert(probe_gb(bus(f), &gh) == InvalidHeader);

    // Synthetic logo data with OSCR's expected 0x4B1B sum; NOT a copy of
    // the Nintendo logo. The test validates the checksum algorithm only.
    for (size_t i = 0; i < f.gba.size(); ++i)
        f.gba[i] = static_cast<uint8_t>(i * 11u + 1u);
    for (size_t i = 4; i < 0xa0; ++i)
        f.gba[i] = static_cast<uint8_t>(0x7b + (i < 4 + 39 ? 1 : 0));
    memcpy(&f.gba[0xa0], "GBA TEST1234", 12);
    memcpy(&f.gba[0xac], "A3DE", 4);
    fix_gba_checksum(f);
    GbaHeader ah = {};
    assert(probe_gba(bus(f), &ah) == Ok);
    assert(strcmp(ah.game_code, "A3DE") == 0);
    assert(read_gba_linear(bus(f), sink(f), &ah, 256 * 1024) == UnsupportedLayout);
    assert(f.written.empty());
    assert(read_gba_linear(bus(f), sink(f), &ah, 1024 * 1024) == Ok);
    assert(f.written == f.gba);
    assert(f.write_calls == 64 + (1024u * 1024u) / 512u);
    f.written.clear();
    f.cancellation = true;
    assert(read_gba_linear(bus(f), sink(f), &ah, 1024 * 1024) == Cancelled);
    assert(f.written.empty());
    f.cancellation = false;
    f.gba[0xbd] ^= 1;
    assert(probe_gba(bus(f), &ah) == InvalidHeader);
    return 0;
}
