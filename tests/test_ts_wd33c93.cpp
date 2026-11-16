// WD33C93 core (ts/wd33c93.h) against a scripted fake target.
#include <cstdint>
#include <vector>

#include "check.h"
#include "ts/wd33c93.h"

using namespace ens::ts;

namespace {

struct Fake : ScsiTarget {
    std::vector<uint8_t> data;
    size_t pos = 0;
    uint8_t st = 0;
    bool rdy = true;
    int lastLen = 0;
    void command(const uint8_t*, int len, uint32_t) override { lastLen = len; pos = 0; }
    Phase phase() const override { return pos < data.size() ? Phase::DataIn : Phase::Status; }
    bool ready() const override { return rdy; }
    uint8_t readByte() override { return data[pos++]; }
    void writeByte(uint8_t) override {}
    uint8_t status() const override { return st; }
};

void wr(Wd33c93& w, int r, uint8_t v) { w.write(false, uint8_t(r)); w.write(true, v); }
uint8_t rd(Wd33c93& w, int r) { w.write(false, uint8_t(r)); return w.read(true); }
bool intr(Wd33c93& w) { return (w.read(false) & 0x80) != 0; }

// Programs a Select-and-Transfer of `count` bytes to `id` with the given first CDB byte, then runs it.
void sel(Wd33c93& w, int id, uint8_t op, int count) {
    wr(w, 3, op);
    wr(w, 0x12, 0); wr(w, 0x13, 0); wr(w, 0x14, uint8_t(count));
    wr(w, 0x15, uint8_t(id));
    wr(w, 0x18, 0x09);
}

}  // namespace

TEST(wd_indirect_steps) {
    Wd33c93 w;
    wr(w, 2, 0x55);
    CHECK_EQ(w.address(), 3);
    CHECK_EQ(rd(w, 2), 0x55);
    for (int r : {0x19, 0x18}) {
        w.write(false, uint8_t(r));
        w.read(true); w.read(true);
        CHECK_EQ(w.address(), r);
    }
}

TEST(wd_probe_values) {
    Wd33c93 w;
    for (int v : {0x00, 0x55, 0xAA, 0xFF}) {
        wr(w, 2, uint8_t(v));
        CHECK_EQ(rd(w, 2), v);
    }
}

TEST(wd_reset_command) {
    Wd33c93 w;
    wr(w, 0, 3);
    wr(w, 0x18, 0x00);
    CHECK(intr(w));
    CHECK_EQ(rd(w, 0x17), 0x00);
    CHECK(!intr(w));
}

TEST(wd_select_timeout) {
    Wd33c93 w;
    sel(w, 5, 0x00, 0);
    CHECK(intr(w));
    CHECK_EQ(rd(w, 0x17), 0x42);
    CHECK_EQ(rd(w, 0x10), 0x00);
}

TEST(wd_select_and_transfer_in) {
    Wd33c93 w;
    Fake f;
    f.data = {0xA1, 0xA2, 0xA3};
    w.attach(4, &f);
    sel(w, 4, 0x12, 3);
    CHECK(w.read(false) & 1);
    CHECK(!intr(w));
    CHECK_EQ(rd(w, 0x14), 3);
    w.write(false, 0x19);
    CHECK_EQ(w.read(true), 0xA1);
    CHECK_EQ(w.read(true), 0xA2);
    CHECK_EQ(w.read(true), 0xA3);
    CHECK(intr(w));
    CHECK_EQ(rd(w, 0x14), 0);
    CHECK_EQ(rd(w, 0x17), 0x16);
    CHECK_EQ(rd(w, 0x10), 0x60);
    CHECK_EQ(rd(w, 0x0F), 0x00);
    CHECK(intr(w));
    CHECK_EQ(rd(w, 0x17), 0x85);
    CHECK(!intr(w));
}

TEST(wd_dbr_waits) {
    Wd33c93 w;
    Fake f;
    f.data = {1};
    f.rdy = false;
    w.attach(4, &f);
    sel(w, 4, 0x28, 1);
    for (int i = 0; i < 3; i++) CHECK_EQ(w.read(false) & 0x81, 0);
    f.rdy = true;
    CHECK_EQ(w.read(false) & 0x81, 1);
}

TEST(wd_check_condition_status) {
    Wd33c93 w;
    Fake f;
    f.st = 0x02;
    w.attach(4, &f);
    sel(w, 4, 0x00, 0);
    CHECK(intr(w));
    CHECK_EQ(rd(w, 0x17), 0x16);
    CHECK_EQ(rd(w, 0x0F), 0x02);
}

TEST(wd_command_while_int_is_lci) {
    Wd33c93 w;
    Fake f;
    w.attach(4, &f);
    wr(w, 0x18, 0x00);              // Reset leaves INT set
    CHECK(intr(w));
    sel(w, 4, 0x12, 0);
    CHECK(w.read(false) & 0x40);
    CHECK_EQ(f.lastLen, 0);         // never reached the target
    CHECK(w.read(false) & 0x80);
}

TEST(wd_cdb_group_length) {
    Wd33c93 w;
    Fake f;
    w.attach(4, &f);
    sel(w, 4, 0x28, 0);
    CHECK_EQ(f.lastLen, 10);
    rd(w, 0x17); rd(w, 0x17);
    sel(w, 4, 0x12, 0);
    CHECK_EQ(f.lastLen, 6);
}
