// CD-ROM SCSI target (ts/cd_target.h) driven directly, with the sector port answered from a synthetic disc.
#include <cstdint>
#include <vector>

#include "check.h"
#include "ts/cd_target.h"

using namespace ens::ts;

namespace {

// Sector n is filled with uint8_t(n); byte 0 of each 512-byte quarter is the quarter index.
void synth(uint32_t lba, uint8_t* out) {
    for (int i = 0; i < SectorPort::kBytes; i++) out[i] = uint8_t(lba);
    for (int q = 0; q < 4; q++) out[q * 512] = uint8_t(q);
}

struct Rig {
    SectorPort port;
    CdTarget t{port};
    // Answers the pending ask, if any, from the synthetic disc.
    void serve(bool ok = true) {
        uint32_t m, l;
        if (!port.pending(m, l)) return;
        uint8_t b[SectorPort::kBytes];
        synth(l, b);
        port.reply(m, l, b, ok);
    }
    // Runs a command that moves data in; serves the port whenever the target waits.
    std::vector<uint8_t> in(const std::vector<uint8_t>& cdb, uint32_t count) {
        t.command(cdb.data(), (int)cdb.size(), count);
        std::vector<uint8_t> v;
        while (t.phase() == ScsiTarget::Phase::DataIn) {
            if (!t.ready()) { serve(); if (!t.ready()) break; }
            v.push_back(t.readByte());
        }
        return v;
    }
    uint8_t simple(const std::vector<uint8_t>& cdb) { in(cdb, 0); return t.status(); }
    std::vector<uint8_t> sense() { return in({0x03, 0, 0, 0, 18, 0}, 18); }
};

const std::vector<uint8_t> kTur = {0, 0, 0, 0, 0, 0};
std::vector<uint8_t> read10(uint32_t lba, uint16_t n) {
    return {0x28, 0, uint8_t(lba >> 24), uint8_t(lba >> 16), uint8_t(lba >> 8), uint8_t(lba), 0, uint8_t(n >> 8),
            uint8_t(n), 0};
}
void modeSelect(Rig& r, uint32_t len) {
    uint8_t d[12] = {0, 0, 0, 8, 0, 0, 0, 0, 0, uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)};
    uint8_t cdb[6] = {0x15, 0x10, 0, 0, 12, 0};
    r.t.command(cdb, 6, 12);
    for (int i = 0; i < 12; i++) { CHECK(r.t.phase() == ScsiTarget::Phase::DataOut); r.t.writeByte(d[i]); }
}

}  // namespace

TEST(cd_target_inquiry) {
    Rig r;
    auto v = r.in({0x12, 0, 0, 0, 0x24, 0}, 0x24);
    CHECK_EQ(v.size(), 0x24u);
    CHECK_EQ(v[0], 5);
    CHECK(v[1] & 0x80);
    CHECK_EQ(v[2], 2);
    CHECK_EQ(r.t.status(), 0);
}

TEST(cd_target_not_ready_then_attention) {
    Rig r;
    CHECK_EQ(r.simple(kTur), 2);
    auto s = r.sense();
    CHECK_EQ(s[0], 0x70);
    CHECK_EQ(s[2], 2);
    CHECK_EQ(s[12], 0x3A);
    r.t.insert(7, 100);
    CHECK_EQ(r.simple(kTur), 2);
    s = r.sense();
    CHECK_EQ(s[2], 6);
    CHECK_EQ(s[12], 0x28);
    CHECK_EQ(r.simple(kTur), 0);
}

TEST(cd_target_mode_select_512) {
    Rig r;
    r.t.insert(7, 100);
    r.simple(kTur);
    r.sense();
    auto cap = r.in({0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8);
    CHECK_EQ(cap[3], 99);  // 2048-byte blocks by default
    modeSelect(r, 512);
    CHECK_EQ(r.t.status(), 0);
    cap = r.in({0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8);
    CHECK_EQ(cap.size(), 8u);
    CHECK_EQ((cap[0] << 24) | (cap[1] << 16) | (cap[2] << 8) | cap[3], 399);
    CHECK_EQ((cap[6] << 8) | cap[7], 512);
    auto d = r.in(read10(5, 1), 512);
    CHECK_EQ(d.size(), 512u);
    CHECK_EQ(d[0], 1);  // second quarter of sector 1
    CHECK_EQ(d[1], 1);
    CHECK_EQ(d[511], 1);
    auto ms = r.in({0x1A, 0, 0, 0, 12, 0}, 12);
    CHECK_EQ(ms.size(), 12u);
    CHECK_EQ((ms[9] << 16) | (ms[10] << 8) | ms[11], 512);
}

TEST(cd_target_mode_select_bad_length) {
    Rig r;
    r.t.insert(7, 100);
    r.simple(kTur);
    modeSelect(r, 1024);
    CHECK_EQ(r.t.status(), 2);
    auto s = r.sense();
    CHECK_EQ(s[2], 5);
    CHECK_EQ(s[12], 0x26);
}

TEST(cd_target_waits_for_sector) {
    Rig r;
    r.t.insert(7, 100);
    r.simple(kTur);
    r.sense();
    auto cdb = read10(3, 1);
    r.t.command(cdb.data(), 10, 2048);
    CHECK(r.t.phase() == ScsiTarget::Phase::DataIn);
    CHECK(!r.t.ready());
    CHECK(!r.t.ready());
    r.serve();
    CHECK(r.t.ready());
    for (int i = 0; i < 2048; i++) {
        CHECK(r.t.ready());
        uint8_t b = r.t.readByte();
        CHECK_EQ(b, (i % 512 == 0) ? (i / 512) : 3);
    }
    CHECK(r.t.phase() == ScsiTarget::Phase::Status);
    CHECK_EQ(r.t.status(), 0);
}

TEST(cd_target_pads_and_cuts) {
    Rig r;
    auto v = r.in({0x12, 0, 0, 0, 0x30, 0}, 0x30);
    CHECK_EQ(v.size(), 0x30u);
    for (size_t i = 36; i < v.size(); i++) CHECK_EQ(v[i], 0);
    CHECK_EQ(r.in({0x03, 0, 0, 0, 4, 0}, 4).size(), 4u);
}

TEST(cd_target_write_protected) {
    Rig r;
    r.t.insert(7, 100);
    r.simple(kTur);
    r.sense();
    CHECK_EQ(r.simple({0x2A, 0, 0, 0, 0, 0, 0, 0, 1, 0}), 2);
    CHECK_EQ(r.sense()[2], 7);
    CHECK_EQ(r.simple({0xFF, 0, 0, 0, 0, 0}), 2);
    auto s = r.sense();
    CHECK_EQ(s[2], 5);
    CHECK_EQ(s[12], 0x20);
}

TEST(cd_target_medium_error) {
    Rig r;
    r.t.insert(7, 100);
    r.simple(kTur);
    r.sense();
    auto cdb = read10(3, 1);
    r.t.command(cdb.data(), 10, 2048);
    CHECK(!r.t.ready());
    r.serve(false);
    size_t n = 0;
    while (r.t.phase() == ScsiTarget::Phase::DataIn) { CHECK(r.t.ready()); r.t.readByte(); n++; }
    CHECK_EQ(n, 2048u);
    CHECK_EQ(r.t.status(), 2);
    auto s = r.sense();
    CHECK_EQ(s[2], 3);
    CHECK_EQ(s[12], 0x11);
}

TEST(cd_target_ignores_stale_reply) {
    Rig r;
    r.t.insert(7, 100);
    r.simple(kTur);
    r.sense();
    auto cdb = read10(3, 1);
    r.t.command(cdb.data(), 10, 2048);
    CHECK(!r.t.ready());              // asked medium 7, lba 3
    r.t.insert(8, 100);               // new disc while the sector is on its way
    r.serve();                        // the late reply for medium 7
    r.simple(kTur);                   // UNIT ATTENTION
    r.sense();
    r.t.command(cdb.data(), 10, 2048);
    CHECK(!r.t.ready());              // medium 8 waits for its own reply
    r.serve();
    CHECK(r.t.ready());
}
