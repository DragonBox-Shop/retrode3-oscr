#pragma once
// Hardware-free MegaDrive save metadata and byte layout helpers.
// Header data never grants permission to touch SRAM/FRAM or write registers.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
namespace retrode3 { namespace mdsave {
enum class Kind { None, Odd8, Even8, Both16, ParallelEeprom, Unsupported };
struct Geometry {
    Kind kind; bool advertised; bool valid;
    std::uint32_t start, end, bytes;
};
inline const char *name(Kind k) {
    switch(k) {
    case Kind::None: return "none";
    case Kind::Odd8: return "sram_odd8";
    case Kind::Even8: return "sram_even8";
    case Kind::Both16: return "sram_both16";
    case Kind::ParallelEeprom: return "parallel_eeprom";
    case Kind::Unsupported: return "unsupported";
    }
    return "unsupported";
}
inline std::uint32_t big32(const unsigned char *p) {
    return (static_cast<std::uint32_t>(p[0])<<24) |
           (static_cast<std::uint32_t>(p[1])<<16) |
           (static_cast<std::uint32_t>(p[2])<<8) | p[3];
}
inline Geometry parse(const unsigned char *h, std::size_t size) {
    Geometry g = { Kind::None, false, false, 0, 0, 0 };
    if (!h || size < 0x1bcu || h[0x1b0]!='R' || h[0x1b1]!='A') return g;
    g.advertised=true;
    const unsigned type = (static_cast<unsigned>(h[0x1b2])<<8)|h[0x1b3];
    g.start=big32(h+0x1b4); g.end=big32(h+0x1b8);
    if (type==0xf840u) {g.kind=Kind::ParallelEeprom; return g;}
    if (type==0xf820u) g.kind = (g.start&1u) ? Kind::Odd8 : Kind::Even8;
    else if (type==0xe020u) g.kind=Kind::Both16;
    else {g.kind=Kind::Unsupported; return g;}
    // Explicitly recognize only canonical 0x200000-0x20ffff.
    // The rest needs a separate mapping/safety review.
    if (g.start<0x200000u || g.end>0x20ffffu || g.end<g.start) return g;
    if (g.kind==Kind::Odd8 && g.start==0x200001u && (g.end&1u))
        g.bytes=(g.end-g.start)/2u+1u;
    else if (g.kind==Kind::Even8 && g.start==0x200000u && !(g.end&1u))
        g.bytes=(g.end-g.start)/2u+1u;
    else if (g.kind==Kind::Both16 && g.start==0x200000u && (g.end&1u))
        g.bytes=g.end-g.start+1u;
    g.valid=g.bytes>0&&g.bytes<=65536u;
    if(!g.valid) g.bytes=0;
    return g;
}
inline std::size_t packed_size(Kind k, std::size_t words) {
    if (k!=Kind::Odd8 && k!=Kind::Even8 && k!=Kind::Both16)
        throw std::invalid_argument("not standard SRAM");
    if (k==Kind::Both16 && words>static_cast<std::size_t>(-1)/2u)
        throw std::overflow_error("SRAM overflow");
    return k==Kind::Both16 ? words*2u : words;
}
inline void pack(Kind k,const std::uint16_t *words,std::size_t n,unsigned char *dst) {
    const std::size_t bytes=packed_size(k,n);
    if ((n&&!words)||(bytes&&!dst)) throw std::invalid_argument("missing save buffer");
    for(std::size_t i=0;i<n;++i) {
        if(k==Kind::Odd8) dst[i]=static_cast<unsigned char>(words[i]);
        else if(k==Kind::Even8) dst[i]=static_cast<unsigned char>(words[i]>>8);
        else {
            dst[2u*i]=static_cast<unsigned char>(words[i]>>8);
            dst[2u*i+1u]=static_cast<unsigned char>(words[i]);
        }
    }
}
inline void unpack(Kind k,const unsigned char *src,std::size_t bytes,std::uint16_t *words) {
    packed_size(k,0);
    if(k==Kind::Both16 && (bytes&1u)) throw std::invalid_argument("odd save size");
    const std::size_t n=k==Kind::Both16 ? bytes/2u : bytes;
    if ((bytes&&!src)||(n&&!words)) throw std::invalid_argument("missing save buffer");
    for(std::size_t i=0;i<n;++i) {
        if(k==Kind::Odd8) words[i]=src[i];
        else if(k==Kind::Even8) words[i]=static_cast<std::uint16_t>(src[i])<<8;
        else words[i]=(static_cast<std::uint16_t>(src[2u*i])<<8)|src[2u*i+1u];
    }
}
}}
