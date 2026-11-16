#include "retrode3/read_bus.h"
#include "retrode3-lib.h"

#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace {
int last_mode = -1;
std::uint32_t last_address = 0;
int next_count = -2;
int calls = 0;
void reset() { last_mode = -1; last_address = 0; next_count = -2; calls = 0; }
}

extern "C" int nes_read(std::uint16_t a, void *b, std::uint16_t n, int mode)
{
    last_mode=mode; last_address=a; ++calls;
    for(unsigned i=0;i<n;++i) static_cast<std::uint8_t *>(b)[i] = 0x5a;
    return next_count == -2 ? n : next_count;
}
extern "C" int md_read(std::uint32_t a, void *b, std::uint32_t n, int mode)
{
    last_mode=mode; last_address=a; ++calls;
    for(unsigned i=0;i<n;++i) static_cast<std::uint8_t *>(b)[i] = i == 0 ? 0x12 : 0x34;
    return next_count == -2 ? static_cast<int>(n) : next_count;
}
extern "C" int snes_read(std::uint8_t bank, std::uint16_t a, void *b, std::uint16_t n, int mode)
{
    last_mode=mode; last_address=(static_cast<unsigned>(bank)<<16)|a; ++calls;
    for(unsigned i=0;i<n;++i) static_cast<std::uint8_t *>(b)[i] = 0x99;
    return next_count == -2 ? n : next_count;
}
int main()
{
    retrode3::ReadBus bus;
    reset();
    assert(bus.nes_byte(0x8000, retrode3::NesSpace::Prg) == 0x5a);
    assert(last_address==0x8000 && last_mode==NES_MODE_PRG && calls==1);
    reset();
    assert(bus.md_word(0x80)==0x1234);
    assert(last_address==0x100 && last_mode==MD_MODE_P10 && calls==1);
    reset();
    assert(bus.snes_byte(0xc0,0x8000,retrode3::SnesReadMode::HiRom)==0x99);
    assert(last_address==0xc08000 && last_mode==SNES_MODE_HIROM && calls==1);
    reset();
    bool threw=false;
    try {bus.nes_block(0xfffe, &last_mode, 3, retrode3::NesSpace::Prg);} catch (std::out_of_range const &) {threw=true;}
    assert(threw && calls==0);
    threw=false;
    try {bus.snes_block(0xc0,0xffff,&last_mode,2);} catch (std::out_of_range const &) {threw=true;}
    assert(threw && calls==0);
    threw=false;
    try {bus.md_block(0xffffff,&last_mode,2);} catch (std::out_of_range const &) {threw=true;}
    assert(threw && calls==0);
    next_count=0;
    threw=false;
    try {bus.nes_byte(0x8000,retrode3::NesSpace::Prg);} catch (std::runtime_error const &) {threw=true;}
    assert(threw);
    reset();
    bus.md_block(0x100,nullptr,0);
    assert(calls==0);
    return 0;
}
