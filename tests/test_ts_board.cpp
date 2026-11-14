#include "check.h"
#include "esp/esp.h"
#include "esp/esp_isa.h"
#include "ts/board.h"

using namespace ens;
using namespace ens::ts;
using ens::esp::Instr;
using ens::esp::Op;
using ens::esp::RamOp;

namespace {
constexpr uint8_t kZero = 255, kSer0L = 235, kSer1L = 237;

uint64_t word(uint8_t b, uint8_t a, Op op) { return Instr{kZero, kZero, b, a, op, 0, false, true, RamOp::ReadDelay}.encode(); }

struct Rig {
    WaveMemory waves;
    Board board{waves};
    OttoFrame f{};
    Rig() {
        board.esp().setInstr(0, word(kSer0L, kSer1L, Op::MOV));  // SER0L -> SER1L
        board.esp().setInstr(1, word(kZero, kZero, Op::END));
    }
};
}  // namespace

TEST(ts_route_otto_to_esp_inputs) {
    Rig r;
    r.f.ch[0][0] = 0x12340;
    Audio a = r.board.route(r.f, kEspSlotsPerFrame);
    CHECK_EQ(a.mainL, 0x1234);
}

TEST(ts_route_aux_is_otto_ch3) {
    Rig r;
    r.f.ch[3][0] = -0x12340;
    r.f.ch[3][1] = 0x7FFFF;
    Audio a = r.board.route(r.f, kEspSlotsPerFrame);
    CHECK_EQ(a.auxL, -0x1234);
    CHECK_EQ(a.auxR, 0x7FFF);
}

TEST(ts_route_saturates) {
    Rig r;
    r.f.ch[3][0] = -0x80000;
    r.f.ch[3][1] = 0x7FFFF;
    Audio a = r.board.route(r.f, kEspSlotsPerFrame);
    CHECK_EQ(a.auxL, -32768);
    CHECK_EQ(a.auxR, 32767);
}

TEST(ts_route_drops_ch4_ch5) {
    Rig r;
    r.f.ch[4][0] = r.f.ch[4][1] = r.f.ch[5][0] = r.f.ch[5][1] = 0x7FFFF;
    Audio a = r.board.route(r.f, kEspSlotsPerFrame);
    CHECK_EQ(a.mainL, 0);
    CHECK_EQ(a.mainR, 0);
    CHECK_EQ(a.auxL, 0);
    CHECK_EQ(a.auxR, 0);
}

TEST(ts_esp_dram_is_64k_words) {
    Rig r;
    EspBus& bus = r.board;
    bus.write(false, 0xFFFF, 0xAAAA);
    bus.write(false, 0x10000, 0x5555);
    CHECK_EQ(bus.read(false, 0xFFFF), 0xAAAA);
    CHECK_EQ(bus.read(false, 0), 0x5555);
    CHECK_EQ(bus.read(false, 0x10000), 0x5555);
}
