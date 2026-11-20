#include "retrode3/nes_mmc1_plan.h"
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <vector>

using retrode3::nesmmc1::Step;
using retrode3::nesmmc1::StepKind;
using namespace retrode3::nesmmc1;

static void collect(void *ctx, Step const &step)
{
    static_cast<std::vector<Step> *>(ctx)->push_back(step);
}
static std::vector<Step> make(unsigned p, unsigned c)
{
    std::vector<Step> steps;
    plan(p, c, collect, &steps);
    return steps;
}
static void verify(unsigned p, unsigned c)
{
    const std::vector<Step> steps = make(p, c);
    unsigned bytes_prg = 0, bytes_chr = 0;
    unsigned banks_prg = 0, banks_chr = 0;
    unsigned e000 = 0, resets = 0;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        Step const &s = steps[i];
        if (s.kind == ResetSerial) {
            assert(s.address == 0x8000u && s.value == 0x80u);
            ++resets;
        } else if (s.kind == SerialValue) {
            assert(s.length == 5u && s.value <= 31u);
            assert(s.address == 0x8000u || s.address == 0xa000u ||
                   s.address == 0xe000u);
            if (s.address == 0xe000u) ++e000;
        } else if (s.kind == ReadPrg) {
            assert(s.address == 0x8000u);
            bytes_prg += s.length;
            ++banks_prg;
        } else if (s.kind == ReadChr) {
            assert(s.address == 0u && s.length == 8192u);
            bytes_chr += s.length;
            ++banks_chr;
        } else { assert(false); }
    }
    assert(bytes_prg == p * 1024u);
    assert(bytes_chr == c * 1024u);
    assert(banks_prg == (p == 32u ? 1u : p / 16u));
    assert(banks_chr == c / 8u);
    assert(e000 == (p == 32u ? 0u : p / 16u));
    assert(resets == banks_prg + banks_chr);
}
static void check_invalid(unsigned p, unsigned c)
{
    bool rejected = false;
    try { make(p, c); } catch (std::invalid_argument const &) {
        rejected = true;
    }
    assert(rejected);
}
int main()
{
    for (unsigned p = 32; p <= 512; p *= 2)
        for (unsigned c = 0; c <= 128; c = c ? c * 2 : 8)
            verify(p, c);
    check_invalid(0, 8);
    check_invalid(16, 0);
    check_invalid(1024, 8);
    check_invalid(64, 4);
    check_invalid(64, 256);
    bool rejected = false;
    try { plan(64, 8, NULL, NULL); }
    catch (std::invalid_argument const &) { rejected = true; }
    assert(rejected);

    // Full Sanni sequence for a 64-KiB PRG with one CHR bank.
    const std::vector<Step> s = make(64, 8);
    assert(s.size() == 4u * 4u + 3u);
    for (unsigned bank = 0; bank < 4; ++bank) {
        const unsigned i = bank * 4u;
        assert(s[i].kind == ResetSerial);
        assert(s[i+1].kind == SerialValue && s[i+1].address == 0x8000u && s[i+1].value == 12u);
        assert(s[i+2].kind == SerialValue && s[i+2].address == 0xe000u && s[i+2].value == bank);
        assert(s[i+3].kind == ReadPrg && s[i+3].value == bank);
    }
    assert(s[16].kind == ResetSerial && s[17].address == 0xa000u);
    assert(s[17].value == 0u && s[18].kind == ReadChr);
    const std::vector<Step> upper = make(512, 16);
    // PRG bank 15 and 16: the 512-KiB high bit comes from CHR register.
    assert(upper[15u*5u+2u].address == 0xa000u && upper[15u*5u+2u].value == 0u);
    assert(upper[16u*5u+2u].address == 0xa000u && upper[16u*5u+2u].value == 0x10u);
    assert(upper[16u*5u+3u].address == 0xe000u && upper[16u*5u+3u].value == 16u);
    return 0;
}
