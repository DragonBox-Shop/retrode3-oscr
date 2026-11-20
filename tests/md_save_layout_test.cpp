#include "retrode3/oscr_md_save.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>
using retrode3::mdsave::Kind;
using retrode3::mdsave::Geometry;
static Geometry sample(unsigned type, std::uint32_t start, std::uint32_t end) {
    unsigned char h[512]={};
    h[0x1b0]='R'; h[0x1b1]='A';
    h[0x1b2]=static_cast<unsigned char>(type>>8);
    h[0x1b3]=static_cast<unsigned char>(type);
    for(unsigned i=0;i<4;++i) {
        h[0x1b4+i]=static_cast<unsigned char>(start>>(24-8*i));
        h[0x1b8+i]=static_cast<unsigned char>(end>>(24-8*i));
    }
    return retrode3::mdsave::parse(h,sizeof(h));
}
int main() {
    Geometry g=sample(0xf820,0x200001,0x203fff);
    assert(g.advertised&&g.valid&&g.kind==Kind::Odd8&&g.bytes==8192);
    g=sample(0xf820,0x200000,0x203ffe);
    assert(g.valid&&g.kind==Kind::Even8&&g.bytes==8192);
    g=sample(0xe020,0x200000,0x20ffff);
    assert(g.valid&&g.kind==Kind::Both16&&g.bytes==65536);
    g=sample(0xf840,0x200001,0x203fff);
    assert(!g.valid&&g.kind==Kind::ParallelEeprom);
    g=sample(0xf820,0x300001,0x303fff);
    assert(!g.valid&&g.bytes==0);
    g=sample(0xf820,0x200001,0x203ffe);
    assert(!g.valid);
    g=sample(0xe020,0x200000,0x30ffff);
    assert(!g.valid);
    g=sample(0xe020,0x200001,0x207fff);
    assert(!g.valid);
    g=sample(0x1111,0x200000,0x20ffff);
    assert(g.kind==Kind::Unsupported&&!g.valid);
    unsigned char blank[512]={};
    assert(!retrode3::mdsave::parse(blank,sizeof(blank)).advertised);
    assert(!retrode3::mdsave::parse(blank,100).advertised);
    std::uint16_t words[3]={0x1234,0x56ab,0xcdef},restored[3]={};
    unsigned char img[6]={};
    retrode3::mdsave::pack(Kind::Odd8,words,3,img);
    assert(img[0]==0x34&&img[1]==0xab&&img[2]==0xef);
    retrode3::mdsave::unpack(Kind::Odd8,img,3,restored);
    assert(restored[0]==0x34&&restored[1]==0xab);
    retrode3::mdsave::pack(Kind::Even8,words,3,img);
    assert(img[0]==0x12&&img[1]==0x56&&img[2]==0xcd);
    retrode3::mdsave::unpack(Kind::Even8,img,3,restored);
    assert(restored[0]==0x1200&&restored[1]==0x5600);
    retrode3::mdsave::pack(Kind::Both16,words,3,img);
    assert(img[0]==0x12&&img[1]==0x34&&img[4]==0xcd&&img[5]==0xef);
    retrode3::mdsave::unpack(Kind::Both16,img,6,restored);
    for(unsigned i=0;i<3;++i) assert(restored[i]==words[i]);
    bool threw=false;
    try { retrode3::mdsave::unpack(Kind::Both16,img,5,restored); }
    catch(const std::invalid_argument&) {threw=true;}
    assert(threw);
}
