// OTTO pot A/D (Plan 2 Task 5; OT16).
#include "check.h"
#include "otto_test_util.h"

using ens::Otto;
using namespace otto_reg;

namespace {

void steps(Otto& o, int n) {
    for (int i = 0; i < n; ++i) o.stepVoice();
}

}  // namespace

TEST(otto_par_conversion) {
    FakeMem m;
    Otto o(m);
    o.setPot(0x155);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0u);  // reset value; this read starts a conversion
    steps(o, 512);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0x155u);
}

// Review focus 4: a second read before the conversion ends sees 0x3FF.
TEST(otto_par_read_twice_returns_3ff) {
    FakeMem m;
    Otto o(m);
    o.setPot(0x155);
    ottoRead32(o, 0, PAR);
    steps(o, 100);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0x3FFu);
}

TEST(otto_par_tracks_pot_at_conversion_end) {
    FakeMem m;
    Otto o(m);
    o.setPot(0x080);
    ottoRead32(o, 0, PAR);
    steps(o, 100);
    o.setPot(0x100);  // the pin's value when the conversion ends is what PAR gets
    steps(o, 420);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0x100u);
}
