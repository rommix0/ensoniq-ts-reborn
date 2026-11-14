#include <map>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "ts/machine.h"

using namespace ens::ts;

namespace {

// A RomSet whose OS image holds `words` at the given CPU addresses in 0xC00000-0xC7FFFF (h = even
// bytes, l = odd bytes); the wave ROMs are zero-filled. Reset vectors: SSP 0xFF8000, PC 0xC00100.
RomSet romWith(std::map<uint32_t, std::vector<uint16_t>> words) {
    RomSet r;
    r.osHigh.assign(0x40000, 0);
    r.osLow.assign(0x40000, 0);
    r.u41.assign(0x200000, 0);
    r.u43.assign(0x100000, 0);
    r.u45.assign(0x200000, 0);
    r.u46.assign(0x100000, 0);
    words[0xC00000] = {0x00FF, 0x8000, 0x00C0, 0x0100};
    for (const auto& [addr, ws] : words) {
        uint32_t off = addr - 0xC00000;
        for (uint16_t w : ws) {
            r.osHigh[off >> 1] = static_cast<uint8_t>(w >> 8);
            r.osLow[off >> 1] = static_cast<uint8_t>(w);
            off += 2;
        }
    }
    return r;
}

std::unique_ptr<Machine> machineWith(const std::vector<uint16_t>& code) {
    std::string err;
    auto m = Machine::create(romWith({{0xC00100, code}}), err);
    CHECK(m != nullptr);
    return m;
}

constexpr uint16_t kBraSelf = 0x60FE;

}  // namespace

TEST(ts_machine_rejects_bad_roms) {
    std::string err;
    CHECK(Machine::create(RomSet{}, err) == nullptr);
    CHECK(!err.empty());
    RomSet r = romWith({});
    r.u45.assign(0x1FFFFF, 0);
    err.clear();
    CHECK(Machine::create(r, err) == nullptr);
    CHECK(err.find("u45") != std::string::npos);
}

TEST(ts_ram_write_reads_back) {
    auto m = machineWith({0x33FC, 0x1234, 0x00FF, 0x1000, kBraSelf});
    Audio a;
    m->runFrame(a);
    CHECK_EQ(m->peek16(0xFF1000), 0x1234);
}

TEST(ts_unmapped_access_is_traced) {
    auto m = machineWith({0x33FC, 0x1234, 0x0050, 0x0000, kBraSelf});
    std::vector<BusEvent> ev;
    m->onBusEvent = [&](const BusEvent& e) { ev.push_back(e); };
    Audio a;
    m->runFrame(a);
    CHECK_EQ(ev.size(), 1u);
    if (ev.size() == 1) {
        CHECK(ev[0].dev == Device::Unmapped);
        CHECK_EQ(ev[0].addr, 0x500000u);
        CHECK_EQ(ev[0].value, 0x1234);
        CHECK_EQ(ev[0].size, 2);
        CHECK(ev[0].write);
    }
}

// Review focus 3: 100 frames of 512 CPU clocks, whatever the instruction mix (40 reset cycles included).
TEST(ts_frame_cadence) {
    const std::vector<std::vector<uint16_t>> loops = {
        {0x4E72, 0x2700},                                      // STOP #$2700 (the CPU stopped)
        {0x4E71, 0x60FC},                                      // NOP; BRA
        {0x323C, 0x0003, 0x203C, 0x0000, 0xFFFF, 0x80C1, 0x60F6},  // MOVE #3,D1; loop: MOVE.L #$FFFF,D0; DIVU D1,D0; BRA
    };
    for (const auto& code : loops) {
        auto m = machineWith(code);
        Audio a;
        for (int i = 0; i < 100; ++i) m->runFrame(a);
        // 51 240 = 40 reset clocks + 3 200 slots x 16; up to one instruction's overshoot (DIVU ~140) is carried.
        CHECK(m->cycles() >= 51240 && m->cycles() < 51240 + 160);
    }
}

TEST(ts_peek_has_no_side_effects) {
    auto m = machineWith({kBraSelf});
    int events = 0;
    m->onBusEvent = [&](const BusEvent&) { ++events; };
    CHECK_EQ(m->peek16(0xC00100), kBraSelf);
    CHECK_EQ(m->peek16(0xC80100), kBraSelf);  // mirrored over 0xC00000-0xCFFFFF
    CHECK_EQ(m->peek16(0x000002), 0x8000);    // the reset vector is readable at 0
    CHECK_EQ(m->peek16(0x500000), 0);
    CHECK_EQ(events, 0);
}
