// OTTO pot A/D (Plan 2 Task 5; OT16): a conversion every 2^13 CLKs (512 slots), free-running.
#include "check.h"
#include "otto_test_util.h"

using ens::Otto;
using namespace otto_reg;

namespace {

void steps(Otto& o, int n) {
    for (int i = 0; i < n; ++i) o.stepVoice();
}

}  // namespace

// No read is needed to start a conversion: PAR holds 0 from reset until the first period ends.
TEST(otto_par_conversion) {
    FakeMem m;
    Otto o(m);
    o.setPot(0x155);
    steps(o, 511);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0u);
    steps(o, 1);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0x155u);
}

// A read neither clears PAR nor restarts the conversion: two reads in a row return the same value.
TEST(otto_par_read_twice_returns_same) {
    FakeMem m;
    Otto o(m);
    o.setPot(0x155);
    steps(o, 600);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0x155u);
    steps(o, 100);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0x155u);
}

// PAR gets the pin's value at each period's end and keeps it for the whole next period. This is what the
// TS OS relies on when it selects an input, waits 3-4 ms and averages 8 reads (REF and the pitch wheel
// at power-on): the first read already returns the new input.
TEST(otto_par_tracks_pot_at_conversion_end) {
    FakeMem m;
    Otto o(m);
    o.setPot(0x080);
    steps(o, 100);
    ottoRead32(o, 0, PAR);  // a read mid-period changes nothing
    o.setPot(0x100);        // the pin's value when the period ends (slot 512) is what PAR gets
    steps(o, 412);
    o.setPot(0x200);  // a later pin change reaches PAR only at the next period's end
    steps(o, 511);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0x100u);
    steps(o, 1);
    CHECK_EQ(ottoRead32(o, 0, PAR), 0x200u);
}
