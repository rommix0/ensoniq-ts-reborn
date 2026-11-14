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

std::unique_ptr<Machine> machineWith(const std::vector<uint16_t>& code, bool sqx70 = false) {
    std::string err;
    auto m = Machine::create(romWith({{0xC00100, code}}), err, sqx70);
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
    auto m = machineWith({0x33FC, 0x1234, 0x0010, 0x0000, kBraSelf});  // nothing at 0x100000 (T13)
    std::vector<BusEvent> ev;
    m->onBusEvent = [&](const BusEvent& e) { ev.push_back(e); };
    Audio a;
    m->runFrame(a);
    CHECK_EQ(ev.size(), 1u);
    if (ev.size() == 1) {
        CHECK(ev[0].dev == Device::Unmapped);
        CHECK_EQ(ev[0].addr, 0x100000u);
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
    CHECK_EQ(m->peek16(0x100000), 0);
    CHECK_EQ(events, 0);
}

// T13: OSRAM is seen at 0 and again at 0xFC0000; T4: the host window reaches OTTO bank 2 and stops at the
// installed 1M words (an empty socket reads 0 and does not alias the installed pair).
TEST(ts_ram_alias_and_sample_window) {
    auto m = machineWith({0x33FC, 0x1234, 0x00FF, 0x1000,   // MOVE.W #$1234,$FF1000
                          0x33FC, 0x5678, 0x0040, 0x0002,   // MOVE.W #$5678,$400002
                          0x33FC, 0x9ABC, 0x0060, 0x0002,   // MOVE.W #$9ABC,$600002 (empty socket)
                          kBraSelf});
    Audio a;
    m->runFrame(a);
    CHECK_EQ(m->peek16(0x031000), 0x1234);
    CHECK_EQ(m->peek16(0x400002), 0x5678);
    CHECK_EQ(m->peek16(0x600002), 0);
    CHECK_EQ(m->peek16(0x400002), 0x5678);
}

// T13, SQX-70 fitted: SEQEXP at 0x038000-0x077FFF, apart from OSRAM's top 32 KB (0xFF8000); nothing
// above it. The battery image is OSRAM (256 KB, CPU order, 0xFF8000 at 0x38000) then SEQEXP; loading it
// keeps the RAM and brings the 0-7 overlay back. A wrong size changes nothing.
TEST(ts_seqexp_window_and_ram_image) {
    auto m = machineWith({kBraSelf}, true);
    m->write16(0x000000, 0xAAAA);
    m->write16(0x038000, 0x1111);
    m->write16(0xFF8000, 0x2222);
    m->write16(0x077FFE, 0x3333);
    m->write16(0x078000, 0x4444);  // unmapped
    CHECK_EQ(m->peek16(0x038000), 0x1111);
    CHECK_EQ(m->peek16(0xFF8000), 0x2222);
    CHECK_EQ(m->peek16(0x077FFE), 0x3333);
    CHECK_EQ(m->peek16(0x078000), 0);
    std::vector<uint8_t> img = m->ramImage();
    CHECK_EQ(img.size(), 0x80000u);
    CHECK_EQ(img[0x00000], 0xAA);
    CHECK_EQ(img[0x38000], 0x22);
    CHECK_EQ(img[0x40000], 0x11);
    CHECK_EQ(img[0x7FFFE], 0x33);
    std::string err;
    CHECK(!m->loadRamImage(std::vector<uint8_t>(1, 0), err));
    CHECK(err == "RAM image: expected 524288 bytes, got 1");
    CHECK_EQ(m->peek16(0x038000), 0x1111);
    CHECK_EQ(m->peek16(0x000000), 0xAAAA);
    img[0x40000] = 0x55;
    CHECK(m->loadRamImage(img, err));
    CHECK_EQ(m->peek16(0x038000), 0x5511);
    CHECK_EQ(m->peek16(0x000000), 0x00FF);  // the ROM's reset SSP again, until the OS writes 0-7
}

// T13: a load is a power cycle: the sample DRAM (T4) and the ESP's delay DRAM have no battery and are
// cleared.
TEST(ts_ram_image_load_clears_volatile_memory) {
    auto m = machineWith({kBraSelf});
    m->write16(0x400000, 0x1234);
    m->board().write(false, 5, 0xBEEF);
    std::string err;
    CHECK(m->loadRamImage(m->ramImage(), err));
    CHECK_EQ(m->peek16(0x400000), 0);
    CHECK_EQ(m->board().read(false, 5), 0);
}

// T13, stock TS (the default): SEQEXP's sockets are empty: 0x038000-0x077FFF reads 0, writes are lost,
// and nothing is traced (SEQEXP/ is decoded). OSRAM's top 32 KB stays at 0xFF8000.
TEST(ts_stock_has_no_seqexp) {
    auto m = machineWith({kBraSelf});
    uint32_t traced = 0;
    m->onBusEvent = [&traced](const BusEvent&) { ++traced; };
    m->write16(0x038000, 0x1111);
    m->write16(0xFF8000, 0x2222);
    CHECK_EQ(m->read16(0x038000), 0);
    CHECK_EQ(m->peek16(0xFF8000), 0x2222);
    CHECK_EQ(traced, 0u);
    CHECK_EQ(m->ramImage().size(), 0x80000u);
    CHECK_EQ(m->ramImage()[0x40000], 0);
}

// IEXP (T9): D0 FSW3B/, D1 FSW4B/, D2 PSRB/, D3 PSLB/, active low; the upper bits read high.
TEST(ts_iexp_inputs) {
    auto m = machineWith({kBraSelf});
    CHECK_EQ(m->read8(0x2C0001), 0xFF);
    m->patchSelect(0, true);
    CHECK_EQ(m->read8(0x2C0001), 0xF7);
    m->patchSelect(1, true);
    CHECK_EQ(m->read8(0x2C0001), 0xF3);
    m->footswitch(3, true);
    m->footswitch(4, true);
    CHECK_EQ(m->read8(0x2C0001), 0xF0);
    m->footswitch(1, true);   // Foot Switch 1 goes through the keyboard, not IEXP
    m->patchSelect(2, true);  // no such side
    CHECK_EQ(m->read8(0x2C0001), 0xF0);
    m->patchSelect(0, false);
    m->footswitch(3, false);
    CHECK_EQ(m->read8(0x2C0001), 0xF9);  // FSW4 and the right patch select still down
}
