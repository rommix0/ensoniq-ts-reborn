// OTTO voice scan, address generation, loop modes and interrupts (Plan 2 Task 2; OT4, OT5, OT14).
#include "check.h"
#include "otto_test_util.h"

using ens::Otto;
using namespace otto_reg;

namespace {

constexpr uint32_t S = 2048;  // one sample in ACCUM units (21.11)
constexpr uint32_t STOP0 = 0x1, STOP1 = 0x2, LEI = 0x4, LPE = 0x8, BLE = 0x10, IRQE = 0x20, DIR = 0x40,
                   IRQ = 0x80;

// Voice v: CR, FC, and START/END/ACCUM given in ACCUM units.
void setup(Otto& o, uint8_t v, uint32_t cr, uint32_t fc, uint32_t start, uint32_t end, uint32_t acc) {
    ottoWrite32(o, v, FC, fc);
    ottoWrite32(o, 0x20 + v, START, start);
    ottoWrite32(o, 0x20 + v, END, end);
    ottoWrite32(o, 0x20 + v, ACCUM, acc);
    ottoWrite32(o, v, CR, cr);
}

uint32_t cr(Otto& o, uint8_t v) { return ottoRead32(o, v, CR); }

struct Rig {
    FakeMem m;
    Otto o{m};
    explicit Rig(uint32_t actv = 0) { ottoWrite32(o, 0, ACTV, actv); }
};

}  // namespace

TEST(otto_forward_step) {
    Rig r;
    setup(r.o, 0, 0, 0x800, 0, 100 * S, 10 * S);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 11 * S);
}

TEST(otto_fraction_step) {
    Rig r;
    setup(r.o, 0, 0, 0x600, 0, 100 * S, 0);
    for (int i = 0; i < 4; ++i) r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 3 * S);
}

TEST(otto_oneshot_stops_after_end) {
    Rig r;
    setup(r.o, 0, 0, 0x800, 0, 100 * S, 99 * S);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 100 * S);  // equal to END is allowed (strict compare)
    CHECK_EQ(cr(r.o, 0) & STOP0, 0u);
    r.o.runFrame();
    CHECK_EQ(cr(r.o, 0) & STOP0, STOP0);
    CHECK_EQ(r.o.accum(0), 100 * S);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 100 * S);
}

TEST(otto_end_fraction_is_used) {
    Rig r;
    setup(r.o, 0, 0, 0x400, 0, 20 * S + 0x400, 20 * S);  // END = 20.5
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 20 * S + 0x400);
    CHECK_EQ(cr(r.o, 0) & STOP0, 0u);
    r.o.runFrame();
    CHECK_EQ(cr(r.o, 0) & STOP0, STOP0);
    CHECK_EQ(r.o.accum(0), 20 * S + 0x400);
}

TEST(otto_forward_loop_keeps_remainder) {
    Rig r;
    setup(r.o, 0, LPE, 0xC00, 10 * S, 20 * S, 19 * S);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 10 * S + 0x400);
}

TEST(otto_bidi_reflects_and_toggles_dir) {
    Rig r;
    setup(r.o, 0, LPE | BLE, 0xC00, 10 * S, 20 * S, 19 * S);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 19 * S + 0x400);
    CHECK_EQ(cr(r.o, 0) & DIR, DIR);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 18 * S);
}

TEST(otto_reverse_loop) {
    Rig r;
    setup(r.o, 0, DIR | LPE, 0x800, 10 * S, 20 * S, 10 * S);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 19 * S);
}

TEST(otto_reverse_oneshot_stops_at_start) {
    Rig r;
    setup(r.o, 0, DIR, 0x800, 10 * S, 20 * S, 10 * S);
    r.o.runFrame();
    CHECK_EQ(cr(r.o, 0) & STOP0, STOP0);
    CHECK_EQ(r.o.accum(0), 10 * S);
}

TEST(otto_transwave_jumps_then_runs_on) {
    Rig r;
    setup(r.o, 0, BLE, 0xC00, 10 * S, 20 * S, 19 * S);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 10 * S + 0x400);
    CHECK_EQ(cr(r.o, 0) & (BLE | LEI), LEI);
    for (int i = 0; i < 8; ++i) r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 22 * S + 0x400);
    CHECK_EQ(cr(r.o, 0) & STOP0, 0u);
}

TEST(otto_lei_ignores_end) {
    Rig r;
    setup(r.o, 0, LEI | IRQE, 0x800, 0, 20 * S, 19 * S);
    for (int i = 0; i < 3; ++i) r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 22 * S);
    CHECK_EQ(cr(r.o, 0) & STOP0, 0u);
    CHECK(!r.o.irqAsserted());
}

TEST(otto_stop1_freezes_address_but_fetches) {
    Rig r;
    setup(r.o, 0, STOP1, 0x800, 0, 100 * S, 50 * S);
    r.o.runFrame();
    CHECK_EQ(r.o.accum(0), 50 * S);
    CHECK(r.m.sawRead(0, 50));
    CHECK(r.m.sawRead(0, 51));
}

TEST(otto_fetch_addresses) {
    Rig r;
    setup(r.o, 0, DIR | 0x8000, 0x800, 0, 100 * S, 50 * S);  // BS1:BS0 = 10 -> bank 2
    r.o.runFrame();
    CHECK(r.m.sawRead(2, 50));
    CHECK(r.m.sawRead(2, 51));
    Rig w;
    setup(w.o, 0, LEI, 0, 0, 0, 0x1FFFFFu * S);
    w.o.runFrame();
    CHECK(w.m.sawRead(0, 0x1FFFFF));
    CHECK(w.m.sawRead(0, 0));
}

TEST(otto_irq_sequence) {
    Rig r(7);
    setup(r.o, 3, IRQE, 0x800, 0, 100 * S, 100 * S);  // crosses END in the first frame
    r.o.runFrame();
    CHECK(r.o.irqAsserted());
    CHECK_EQ(ottoRead32(r.o, 0, IRQV), 0x03u);
    CHECK(!r.o.irqAsserted());
    r.o.runFrame();
    CHECK_EQ(cr(r.o, 3) & IRQ, 0u);
    CHECK(!r.o.irqAsserted());

    Rig t(7);
    setup(t.o, 2, IRQE, 0x800, 0, 100 * S, 100 * S);
    setup(t.o, 5, IRQE, 0x800, 0, 100 * S, 100 * S);
    t.o.runFrame();
    CHECK_EQ(ottoRead32(t.o, 0, IRQV), 0x02u);
    t.o.runFrame();
    CHECK(t.o.irqAsserted());
    CHECK_EQ(ottoRead32(t.o, 0, IRQV), 0x05u);
}

TEST(otto_forced_irq) {
    Rig r(7);
    ottoWrite32(r.o, 4, CR, IRQE | IRQ | STOP0 | STOP1);
    r.o.runFrame();
    CHECK(r.o.irqAsserted());
    CHECK_EQ(ottoRead32(r.o, 0, IRQV), 0x04u);
}

// Review focus 4: reading IRQV with nothing pending neither invents nor loses an interrupt.
TEST(otto_irqv_read_without_pending_is_harmless) {
    Rig r;
    CHECK_EQ(ottoRead32(r.o, 0, IRQV), 0x80u);
    CHECK_EQ(ottoRead32(r.o, 0, IRQV), 0x80u);
    CHECK(!r.o.irqAsserted());
    setup(r.o, 0, IRQE, 0x800, 0, 100 * S, 100 * S);
    r.o.runFrame();
    CHECK(r.o.irqAsserted());
    CHECK_EQ(ottoRead32(r.o, 0, IRQV), 0x00u);
}
