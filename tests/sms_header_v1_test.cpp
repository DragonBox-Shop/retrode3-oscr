#include "retrode3/sms_header_v1.h"

#include <cassert>
#include <cstdint>
#include <cstring>

int main()
{
    const struct { std::uint8_t code; std::uint32_t bytes; } cases[] = {
        {0xA, 8192u}, {0xB, 16384u}, {0xC, 32768u},
        {0xD, 49152u}, {0xE, 65536u}, {0xF, 131072u},
        {0x0, 262144u}, {0x1, 524288u}, {0x2, 524288u}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        std::uint8_t sample[16] = {};
        std::memcpy(sample, "TMR SEGA", 8);
        sample[10] = 0x34;
        sample[11] = 0x12;
        sample[15] = static_cast<std::uint8_t>(0x40u | cases[i].code);
        retrode3::sms::Header got = retrode3::sms::Header();
        assert(retrode3::sms::decode_header(sample, sizeof(sample), &got));
        assert(got.declared_rom_bytes == cases[i].bytes);
        assert(got.checksum == 0x1234u);
        assert(got.region_code == 4u);
        assert(got.size_code == cases[i].code);
    }
    // Header product number 5055 is BCD 55 50, with low revision
    // nibble independent of the product code.
    std::uint8_t product[16] = {};
    std::memcpy(product, "TMR SEGA", 8);
    product[12] = 0x55;
    product[13] = 0x50;
    product[14] = 0x01;
    product[15] = 0x4f;
    retrode3::sms::Header decoded = retrode3::sms::Header();
    assert(retrode3::sms::decode_header(product, 16, &decoded));
    assert(decoded.product_code_valid);
    assert(decoded.product_code == 5055u);
    assert(decoded.revision == 1u);
    product[12] = 0xff;
    assert(retrode3::sms::decode_header(product, 16, &decoded));
    assert(!decoded.product_code_valid);
    product[12] = 0x55;
    product[14] = 0xa3;
    assert(retrode3::sms::decode_header(product, 16, &decoded));
    assert(decoded.product_code == 105055u);
    assert(decoded.revision == 3u);

    std::uint8_t sample[16] = {};
    std::memcpy(sample, "TMR SEGA", 8);
    sample[15] = 0x4Cu;
    retrode3::sms::Header got = retrode3::sms::Header();
    assert(!retrode3::sms::decode_header(sample, 15, &got));
    assert(!retrode3::sms::decode_header(NULL, 16, &got));
    assert(!retrode3::sms::decode_header(sample, 16, NULL));
    sample[0] = 'X';
    assert(!retrode3::sms::decode_header(sample, 16, &got));
    sample[0] = 'T';
    for (unsigned size_code = 3; size_code <= 9; ++size_code) {
        sample[15] = static_cast<std::uint8_t>(0x40u | size_code);
        assert(!retrode3::sms::decode_header(sample, 16, &got));
    }
    // Failed parse leaves the caller's metadata unchanged.
    assert(got.declared_rom_bytes == 0u);
    std::uint32_t bytes = 999u;
    assert(!retrode3::sms::decode_size(0xFFu, &bytes));
    assert(bytes == 999u);
    assert(!retrode3::sms::decode_size(0xCu, NULL));
    return 0;
}
