// OTTO volume, envelopes, channels and stopped voices (Plan 2 Task 4; OT7, OT12, OT13, OT15, OT19).
#include "check.h"
#include "otto_test_util.h"

using ens::Otto;
using ens::otto_vol;
using namespace otto_reg;

namespace {

constexpr uint32_t STOP0 = 0x1, LP3 = 0x100, LP4 = 0x200;
constexpr uint32_t CA(uint32_t n) { return n << 10; }

// A "DC voice": constant input, mode 11, K1 = K2 = 0xFFF0, LVOL 0xFFF0, RVOL 0, FC 0, no loop.
// Settled, its filter output is x16 = 16382 and its left contribution otto_vol(16382, 0xFFF0).
void dcVoice(Otto& o, uint8_t v, uint32_t extraCr = 0) {
    ottoWrite32(o, v, K1, 0xFFF0);
    ottoWrite32(o, v, K2, 0xFFF0);
    ottoWrite32(o, v, LVOL, 0xFFF0);
    ottoWrite32(o, v, RVOL, 0);
    ottoWrite32(o, v, CR, LP4 | LP3 | extraCr);
}

struct Rig {
    FakeMem m;
    Otto o{m};
    explicit Rig(uint32_t actv, uint16_t fill = 0x4000) {
        m.fill = fill;
        ottoWrite32(o, 0, ACTV, actv);
    }
    ens::OttoFrame settle(int frames = 400) {
        ens::OttoFrame f{};
        for (int i = 0; i < frames; ++i) f = o.runFrame();
        return f;
    }
};

const int32_t kDc = otto_vol(16382, 0xFFF0);  // 261600

}  // namespace

TEST(otto_vol_values) {
    CHECK_EQ(otto_vol(0x4000, 0xF000), 131072);
    CHECK_EQ(otto_vol(0x4000, 0xFFF0), 261632);
    CHECK_EQ(otto_vol(-32768, 0xF000), -262144);
    CHECK_EQ(otto_vol(32767, 0xFFF0), 523248);
    CHECK_EQ(otto_vol(1000, 0x0000), 0);
    CHECK_EQ(otto_vol(-1, 0x0000), -1);
    CHECK_EQ(otto_vol(0x4000, 0xE800), 98304);
}

TEST(otto_ramp_values) {
    CHECK_EQ(ens::otto_ramp(0x1000, 0x10), 0x1010);
    CHECK_EQ(ens::otto_ramp(0x0008, 0xF0), 0);
    CHECK_EQ(ens::otto_ramp(0xFFFA, 0x10), 0xFFFF);
}

TEST(otto_dc_voice_output) {
    Rig r(0);
    dcVoice(r.o, 0);
    const auto f = r.settle();
    CHECK_EQ(kDc, 261600);
    CHECK_EQ(f.ch[0][0], kDc);
    CHECK_EQ(f.ch[0][1], otto_vol(16382, 0));  // 3: exponent 0 is not a mute (OT12)
    for (int c = 1; c < 6; ++c) CHECK(f.ch[c][0] == 0 && f.ch[c][1] == 0);
}

TEST(otto_ca_selects_channel) {
    Rig r(0);
    dcVoice(r.o, 0, CA(5));
    auto f = r.settle();
    CHECK_EQ(f.ch[5][0], kDc);
    for (int c = 0; c < 5; ++c) CHECK(f.ch[c][0] == 0 && f.ch[c][1] == 0);
    Rig q(0);
    dcVoice(q.o, 0, CA(6));
    f = q.settle();
    for (int c = 0; c < 6; ++c) CHECK(f.ch[c][0] == 0 && f.ch[c][1] == 0);
}

TEST(otto_volume_ramp) {
    Rig r(0);
    ottoWrite32(r.o, 0, LVOL, 0x1000);
    ottoWrite32(r.o, 0, LVRAMP, 0x1000);  // +16 in bits 15:8
    ottoWrite32(r.o, 0, ECOUNT, 3);
    ottoWrite32(r.o, 0, CR, 0);
    for (int i = 0; i < 5; ++i) r.o.runFrame();
    CHECK_EQ(ottoRead32(r.o, 0, LVOL), 0x1030u);
    CHECK_EQ(ottoRead32(r.o, 0, ECOUNT), 0u);
    Rig d(0);
    ottoWrite32(d.o, 0, LVOL, 0x0008);
    ottoWrite32(d.o, 0, LVRAMP, 0xF000);  // -16
    ottoWrite32(d.o, 0, ECOUNT, 2);
    ottoWrite32(d.o, 0, CR, 0);
    for (int i = 0; i < 3; ++i) d.o.runFrame();
    CHECK_EQ(ottoRead32(d.o, 0, LVOL), 0u);
}

TEST(otto_filter_ramp_slow) {
    Rig r(0);
    ottoWrite32(r.o, 0, K1RAMP, 0x1001);  // +16, SLOW
    ottoWrite32(r.o, 0, K2RAMP, 0x1000);  // +16
    ottoWrite32(r.o, 0, ECOUNT, 16);
    ottoWrite32(r.o, 0, CR, 0);
    for (int i = 0; i < 16; ++i) r.o.runFrame();
    CHECK_EQ(ottoRead32(r.o, 0, K1), 32u);  // FILTCOUNT 0 only: frames 0 and 8
    CHECK_EQ(ottoRead32(r.o, 0, K2), 256u);
    CHECK_EQ(ottoRead32(r.o, 0, ECOUNT), 0u);
}

TEST(otto_stopped_voice_is_silent_and_frozen) {
    Rig r(0);
    dcVoice(r.o, 0, STOP0);
    ottoWrite32(r.o, 0, LVRAMP, 0x1000);
    ottoWrite32(r.o, 0, ECOUNT, 5);
    const auto f = r.settle();
    CHECK(f.ch[0][0] == 0 && f.ch[0][1] == 0);
    CHECK_EQ(ottoRead32(r.o, 0, LVOL), 0xFFF0u);
    CHECK_EQ(ottoRead32(r.o, 0, ECOUNT), 5u);
    CHECK_EQ(ottoRead32(r.o, 0x20, O1_1), 32767u);  // OT7: the filter still runs
}

TEST(otto_channel_saturation) {
    Rig r(3);
    for (uint8_t v = 0; v < 4; ++v) dcVoice(r.o, v, CA(1));
    CHECK_EQ(r.settle().ch[1][0], 524287);
    Rig n(3, 0xC000);
    for (uint8_t v = 0; v < 4; ++v) dcVoice(n.o, v, CA(1));
    CHECK_EQ(n.settle().ch[1][0], -524288);
}

// Review focus 3: 32 full-scale voices on one channel clip at the 20-bit rail.
TEST(otto_channel_saturates_not_wraps) {
    Rig r(31);
    for (uint8_t v = 0; v < 32; ++v) dcVoice(r.o, v);
    CHECK_EQ(r.settle().ch[0][0], 524287);
}

// Review focus 1: a write between slots affects the voices after it in this frame, not before.
TEST(otto_midframe_write_affects_later_voices_only) {
    Rig r(7);
    dcVoice(r.o, 2);
    dcVoice(r.o, 6);
    r.settle();
    for (int i = 0; i < 4; ++i) r.o.stepVoice();  // slots 0-3: voice 2 done
    ottoWrite32(r.o, 2, LVOL, 0);
    ottoWrite32(r.o, 6, LVOL, 0);
    while (!r.o.frameReady()) r.o.stepVoice();
    CHECK_EQ(r.o.takeFrame().ch[0][0], kDc + otto_vol(16382, 0));
}

// Review focus 5: fewer than 5 voices still makes frames (OT19).
TEST(otto_actv_below_4_still_produces_frames) {
    Rig r(2);
    dcVoice(r.o, 0);
    r.settle();
    int slots = 0;
    do {
        r.o.stepVoice();
        ++slots;
    } while (!r.o.frameReady());
    CHECK_EQ(slots, 3);
    CHECK_EQ(r.o.takeFrame().ch[0][0], kDc);
}

TEST(otto_test_page_drives_output) {
    Rig r(0);
    dcVoice(r.o, 0);
    ottoWrite32(r.o, 0x40, 4, 0x123456);  // CH2L: positive 23-bit, 1193046
    ottoWrite32(r.o, 0x40, 7, 0x7FFFFF);  // CH3R: 23-bit -1
    const auto f = r.o.runFrame();         // PAGE stays 0x40 (TEST set)
    CHECK_EQ(f.ch[2][0], 524287);
    CHECK_EQ(f.ch[3][1], -1);
    CHECK_EQ(f.ch[0][0], 0);
}
