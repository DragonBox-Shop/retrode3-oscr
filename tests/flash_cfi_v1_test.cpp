#include "retrode3/flash_cfi_v1.h"
#include "retrode3/core_flash_v1.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace retrode3::flash;

static void set16(std::vector<std::uint8_t> &data, unsigned n, unsigned v) {
    data[n] = static_cast<std::uint8_t>(v);
    data[n + 1] = static_cast<std::uint8_t>(v >> 8);
}
static std::vector<std::uint8_t> cfi_4mb() {
    std::vector<std::uint8_t> t(0x80, 0);
    t[0x10] = 'Q'; t[0x11] = 'R'; t[0x12] = 'Y';
    set16(t, 0x13, 2); // AMD/Fujitsu command set
    set16(t, 0x28, 2); // x8/x16 interface
    t[0x27] = 22;      // 4 MiB
    t[0x1f] = 6; t[0x23] = 4;
    t[0x21] = 8; t[0x25] = 4;
    t[0x2c] = 1;
    set16(t, 0x2d, 63); set16(t, 0x2f, 256);
    return t;
}
int main() {
    std::vector<std::uint8_t> t = cfi_4mb();
    CfiGeometry g; std::string why;
    assert(parse_cfi_v1(t.data(), t.size(), g, why));
    assert(g.primary_command_set == 2 && g.capacity_bytes == 4194304u);
    assert(g.erase_regions.size() == 1 && g.erase_regions[0].blocks == 64);
    assert(g.erase_regions[0].block_bytes == 65536);
    assert(g.maximum_word_us == 1024 && g.maximum_erase_ms == 4096);
    std::vector<std::uint64_t> plan;
    assert(erase_plan_v1(g, 65537, plan, why));
    assert(plan.size() == 2 && plan[0] == 0 && plan[1] == 65536);
    assert(!erase_plan_v1(g, 0, plan, why));
    assert(!erase_plan_v1(g, g.capacity_bytes + 1, plan, why));
    t[0x10] = 'X'; assert(!parse_cfi_v1(t.data(), t.size(), g, why));
    // Four mixed boot-block regions, still exactly 4 MiB.
    t = cfi_4mb(); t[0x2c] = 4;
    set16(t, 0x2d, 0);  set16(t, 0x2f, 64);   // 1 x 16 KiB
    set16(t, 0x31, 1);  set16(t, 0x33, 32);   // 2 x 8 KiB
    set16(t, 0x35, 0);  set16(t, 0x37, 128);  // 1 x 32 KiB
    set16(t, 0x39, 62); set16(t, 0x3b, 256); // 63 x 64 KiB
    assert(parse_cfi_v1(t.data(), t.size(), g, why));
    assert(g.erase_regions.size() == 4 && g.erase_regions[3].start_bytes == 65536);
    assert(erase_plan_v1(g, 1, plan, why) && plan.size() == 1);
    assert(erase_plan_v1(g, 65537, plan, why) && plan.size() == 5);
    t = cfi_4mb(); t[0x2c] = 9;
    assert(!parse_cfi_v1(t.data(), t.size(), g, why));
    assert(g.capacity_bytes == 0);
    t = cfi_4mb(); set16(t, 0x2d, 127);
    assert(!parse_cfi_v1(t.data(), t.size(), g, why));
    t = cfi_4mb(); t[0x27] = 63;
    assert(!parse_cfi_v1(t.data(), t.size(), g, why));
    t = cfi_4mb(); t[0x2c] = 2;
    assert(!parse_cfi_v1(t.data(), 0x31, g, why));
    static_assert(RETRODE3_CORE_FLASH_ABI_V1 == 1u, "flash ABI changed");
    return 0;
}
