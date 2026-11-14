// OTTO host interface: byte latches, paging, register masks, reset (Plan 2 Task 1; OT1-OT3).
#include "check.h"
#include "otto_test_util.h"

using ens::Otto;
using namespace otto_reg;

TEST(otto_reset_state) {
    FakeMem m;
    Otto o(m);
    o.reset();
    CHECK_EQ(ottoRead32(o, 0, CR), 0x0003u);
    CHECK_EQ(ottoRead32(o, 0, ACTV), 0x1Fu);
    CHECK_EQ(ottoRead32(o, 0, MODE), 0x17u);
    CHECK_EQ(ottoRead32(o, 0, IRQV), 0x80u);
    CHECK_EQ(o.activeVoices(), 32);
    CHECK(!o.irqAsserted());
}

TEST(otto_page_selects_voice) {
    FakeMem m;
    Otto o(m);
    ottoWrite32(o, 3, FC, 0x12345);
    ottoWrite32(o, 4, FC, 0x00800);
    CHECK_EQ(ottoRead32(o, 3, FC), 0x12345u);
    CHECK_EQ(ottoRead32(o, 4, FC), 0x00800u);
    ottoWrite32(o, 0x25, CR, 0xA5A0);  // CR is the same register on pages v and 0x20+v
    CHECK_EQ(ottoRead32(o, 5, CR), 0xA5A0u);
}

TEST(otto_masks) {
    FakeMem m;
    Otto o(m);
    const struct {
        uint8_t page, reg;
        uint32_t mask;
    } cases[] = {
        {0, FC, 0x1FFFF},      {0, LVRAMP, 0xFF00},      {0, ECOUNT, 0x1FF},      {0, K1RAMP, 0xFF01},
        {0, ACTV, 0x1F},       {0, MODE, 0x1F},          {0x20, START, 0xFFFFF800}, {0x20, END, 0xFFFFFF80},
        {0x20, O1_1, 0x3FFFF}, {0x20, W_ST, 0x7F},
    };
    for (const auto& c : cases) {
        ottoWrite32(o, c.page, c.reg, 0xFFFFFFFF);
        CHECK_EQ(ottoRead32(o, c.page, c.reg), c.mask);
    }
    ottoWrite32(o, 0, PAGE, 0xFFFFFFFF);
    CHECK_EQ(ottoRead32(o, 0x7F, PAGE), 0x7Fu);
    ottoWrite32(o, 0x40, 2, 0x123456);  // CH1L on the test page: write-only
    CHECK_EQ(ottoRead32(o, 0x40, 2), 0u);
}

TEST(otto_common_regs_shared) {
    FakeMem m;
    Otto o(m);
    ottoWrite32(o, 7, ACTV, 9);
    CHECK_EQ(ottoRead32(o, 0, ACTV), 9u);
    CHECK_EQ(o.activeVoices(), 10);
    ottoWrite32(o, 0x21, W_END, 17);
    CHECK_EQ(ottoRead32(o, 0x3F, W_END), 17u);
}

TEST(otto_read_latch) {
    FakeMem m;
    Otto o(m);
    ottoWrite32(o, 0x20, ACCUM, 0x11223344);
    CHECK_EQ(o.read(ACCUM * 4 + 0), 0x11);
    ottoWrite32(o, 0x20, ACCUM, 0xAABBCCDD);
    CHECK_EQ(o.read(ACCUM * 4 + 1), 0x22);  // still the latch of the earlier byte-0 read
    CHECK_EQ(o.read(ACCUM * 4 + 0), 0xAA);
    CHECK_EQ(o.read(ACCUM * 4 + 3), 0xDD);
}

// Review focus 2: bytes 0-2 go to one shared latch; byte 3 commits to whichever register it addresses.
TEST(otto_byte3_commits_shared_latch) {
    FakeMem m;
    Otto o(m);
    ottoSetPage(o, 0);
    o.write(FC * 4 + 0, 0x00);
    o.write(FC * 4 + 1, 0x01);
    o.write(FC * 4 + 2, 0x23);
    o.write(K1 * 4 + 3, 0x40);
    o.write(LVOL * 4 + 3, 0x55);  // a lone byte 3: the latch still holds 00 01 23 after the commit
    CHECK_EQ(ottoRead32(o, 0, K1), 0x2340u);
    CHECK_EQ(ottoRead32(o, 0, FC), 0u);
    CHECK_EQ(ottoRead32(o, 0, LVOL), 0x2355u);
}

TEST(otto_sample_rate) {
    CHECK_EQ(Otto::sampleRateHz(16000000, 32), 31250u);
    CHECK_EQ(Otto::sampleRateHz(16934400, 24), 44100u);
}
