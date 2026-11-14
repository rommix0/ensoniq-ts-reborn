// OTTO sample decode, interpolation and the 4-pole filter (Plan 2 Task 3; OT6, OT8-OT11).
#include <cstdlib>

#include "check.h"
#include "otto_test_util.h"

using ens::Otto;
using namespace otto_reg;

namespace {

// The datasheet's encoder (notes §3), 8-bit form: exp<<5 | 5-bit mantissa.
uint8_t encode(uint16_t w) {
    int e = 7;
    while (e > 0 && ((w >> 15) & 1) == ((w >> 14) & 1)) {
        w = static_cast<uint16_t>(w << 1);
        --e;
    }
    const int m = e > 0 ? (w >> 10) & 0x1F : (w >> 11) & 0x1F;
    return static_cast<uint8_t>(e << 5 | m);
}

// One running voice 0 (ACTV 0) reading a constant word, with the given CR and K values.
struct DcRig {
    FakeMem m;
    Otto o{m};
    DcRig(uint16_t word, uint32_t cr, uint32_t k1, uint32_t k2) {
        m.fill = word;
        ottoWrite32(o, 0, ACTV, 0);
        ottoWrite32(o, 0, K1, k1);
        ottoWrite32(o, 0, K2, k2);
        ottoWrite32(o, 0, CR, cr);
        for (int i = 0; i < 300; ++i) o.runFrame();
    }
    uint32_t temp(uint8_t reg) { return ottoRead32(o, 0x20, reg); }
};

constexpr uint32_t LP3 = 0x100, LP4 = 0x200, CMPD = 0x2000;

}  // namespace

TEST(otto_decompress_examples) {
    CHECK_EQ(ens::otto_decompress(0xFF00), 0x7C00);
    CHECK_EQ(ens::otto_decompress(0xE000), static_cast<int16_t>(0x8000));
    CHECK_EQ(ens::otto_decompress(0x1F00), static_cast<int16_t>(0xFFF0));
    CHECK_EQ(ens::otto_decompress(0x0F00), 0x00F0);
    CHECK_EQ(ens::otto_decompress(0xB200), 0x1200);
    CHECK_EQ(ens::otto_decompress(0xF000), 0x4000);
    CHECK_EQ(ens::otto_decompress(0x0000), 0);
}

TEST(otto_decompress_roundtrip) {
    int bad = 0;
    for (uint32_t w = 0; w <= 0xFFFF; ++w) {
        const uint8_t c = encode(static_cast<uint16_t>(w));
        const auto d = static_cast<uint16_t>(ens::otto_decompress(static_cast<uint16_t>(c << 8)));
        if (encode(d) != c) ++bad;
    }
    CHECK_EQ(bad, 0);
}

TEST(otto_interp_values) {
    CHECK_EQ(ens::otto_interp(0, 512, 256), 512);
    CHECK_EQ(ens::otto_interp(-1, 0, 1), -2);
    CHECK_EQ(ens::otto_interp(100, -100, 511), -200);
    CHECK_EQ(ens::otto_interp(32767, -32768, 511), -65281);
}

TEST(otto_lp_values) {
    CHECK_EQ(ens::otto_lp(2000, 0, 4095), 1999);
    CHECK_EQ(ens::otto_lp(2000, 0, 0), 0);
    CHECK_EQ(ens::otto_lp(-131072, 131071, 4095), -131009);
    CHECK_EQ(ens::otto_lp(1000, 1000, 2048), 1000);
}

TEST(otto_hp_values) {
    CHECK_EQ(ens::otto_hp(2000, 0, 0, 0), 2000);
    CHECK_EQ(ens::otto_hp(0, 0, 1000, 0), 500);
    CHECK_EQ(ens::otto_hp(0, 0, 1000, 2048), 1000);
    CHECK_EQ(ens::otto_hp(0, 0, 131071, 4095), 131071);  // saturated to 18 bits
}

// OT10: K1 = 0xFFF0, K2 = 0, DC input 0x4000 (0x8000 in half-LSB units), 300 frames per mode.
TEST(otto_filter_mode_routing) {
    DcRig m11(0x4000, LP4 | LP3, 0xFFF0, 0);  // pole 3 LP K1, pole 4 LP K2 (holds 0)
    CHECK_EQ(m11.temp(O1_1), 32767u);
    CHECK_EQ(m11.temp(O3_1), 32765u);
    CHECK_EQ(m11.temp(O4_1), 0u);
    DcRig m01(0x4000, LP3, 0xFFF0, 0);  // pole 3 LP K1, pole 4 HP K2 (DC decays)
    CHECK_EQ(m01.temp(O3_1), 32765u);
    CHECK_EQ(m01.temp(O4_1), 0u);
    DcRig m10(0x4000, LP4, 0xFFF0, 0);  // pole 3 LP K2 (holds 0)
    CHECK_EQ(m10.temp(O3_1), 0u);
    CHECK_EQ(m10.temp(O4_1), 0u);
    DcRig m00(0x4000, 0, 0xFFF0, 0);  // both HP K2: pole 4 settles at -1 (floor of -1/2)
    CHECK_EQ(m00.temp(O3_1), 0u);
    CHECK_EQ(m00.temp(O4_1), 0x3FFFFu);
}

TEST(otto_cmpd_voice_decodes) {
    DcRig r(0xF000, CMPD | LP4 | LP3, 0xFFF0, 0xFFF0);  // 0xF000 decodes to 0x4000
    CHECK(r.temp(O4_1) >= 2u * 0x4000 - 8);
    CHECK(r.temp(O4_1) <= 2u * 0x4000);
}
