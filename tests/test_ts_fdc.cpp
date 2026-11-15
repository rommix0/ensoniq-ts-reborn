#include <cstring>
#include <initializer_list>
#include <memory>

#include "check.h"
#include "ts/disk.h"
#include "ts/fdc.h"

using namespace ens::ts;

namespace {

// Drives the two registers the way the OS does: command bytes on A0 = 1, MSR on A0 = 0.
struct Drv {
    Fdc f;
    void cmd(std::initializer_list<int> b) {
        for (int v : b) f.write(true, static_cast<uint8_t>(v));
    }
    bool exm() { return (f.read(false) & 0x20) != 0; }
    bool dio() { return (f.read(false) & 0x40) != 0; }
    // Reads the 7 result bytes of a read/write/format/read-ID command.
    void result7(uint8_t* r) {
        CHECK(!exm());
        CHECK(dio());
        for (int i = 0; i < 7; ++i) r[i] = f.read(true);
        CHECK_EQ(f.read(false), 0x80);  // idle again
    }
};

}  // namespace

TEST(fdc_empty_drive_unchanged) {
    Drv d;
    d.cmd({0x04, 0x00});
    CHECK_EQ(d.f.read(true), 0x00);
    d.cmd({0x46, 0x00, 1, 0, 3, 2, 3, 0x1B, 0xFF});
    uint8_t r[7];
    d.result7(r);
    CHECK_EQ(r[0], 0x48);
    CHECK_EQ(r[3], 1);
}

TEST(fdc_recalibrate_and_seek) {
    auto disk = Disk::formatted(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x07, 0x00});
    d.cmd({0x08});
    CHECK_EQ(d.f.read(true), 0x20);
    CHECK_EQ(d.f.read(true), 0);
    d.cmd({0x0F, 0x00, 0x05});
    d.cmd({0x08});
    CHECK_EQ(d.f.read(true), 0x20);
    CHECK_EQ(d.f.read(true), 5);
    CHECK_EQ(d.f.cylinder(), 5);
    d.cmd({0x08});  // nothing pending now
    CHECK_EQ(d.f.read(true), 0x80);
}

TEST(fdc_sense_drive_status_with_disk) {
    auto disk = Disk::formatted(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x04, 0x00});
    CHECK_EQ(d.f.read(true), 0x20 | 0x08 | 0x10);
    disk->setWriteProtected(true);
    d.cmd({0x04, 0x00});
    CHECK_EQ(d.f.read(true), 0x40 | 0x20 | 0x08 | 0x10);
    d.cmd({0x0F, 0x00, 0x05});
    d.cmd({0x08});
    d.f.read(true); d.f.read(true);
    d.cmd({0x04, 0x00});
    CHECK_EQ(d.f.read(true), 0x40 | 0x20 | 0x08);  // no track 0 away from it
}

TEST(fdc_read_data_non_dma) {
    auto disk = Disk::formatted(Density::Double);
    uint8_t* s = disk->block(23);
    for (int i = 0; i < 512; ++i) s[i] = static_cast<uint8_t>(i);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x46, 0x00, 1, 0, 3, 2, 3, 0x1B, 0xFF});
    for (int i = 0; i < 512; ++i) {
        CHECK(d.exm());
        CHECK(d.dio());
        CHECK_EQ(d.f.read(true), i & 0xFF);
    }
    uint8_t r[7];
    d.result7(r);
    const uint8_t want[7] = {0x40, 0x80, 0, 1, 0, 4, 2};  // EOT without TC (grounded on the TS): IC 01 + EN (T22)
    CHECK(std::memcmp(r, want, 7) == 0);
}

TEST(fdc_read_data_multi_sector) {
    auto disk = Disk::formatted(Density::Double);
    disk->block(2)[0] = 0xA2;
    disk->block(3)[0] = 0xA3;
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x46, 0x00, 0, 0, 2, 2, 3, 0x1B, 0xFF});
    int got = 0;
    uint8_t first[2] = {};
    while (d.exm()) {
        const uint8_t v = d.f.read(true);
        if (got == 0) first[0] = v;
        if (got == 512) first[1] = v;
        ++got;
    }
    CHECK_EQ(got, 1024);
    CHECK_EQ(first[0], 0xA2);
    CHECK_EQ(first[1], 0xA3);
    uint8_t r[7];
    d.result7(r);
    CHECK_EQ(r[5], 4);
}

TEST(fdc_write_data_counts_write) {
    auto disk = Disk::formatted(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    const uint32_t w0 = disk->writes();
    d.cmd({0x45, 0x00, 0, 1, 5, 2, 5, 0x1B, 0xFF});
    for (int i = 0; i < 512; ++i) {
        CHECK(d.exm());
        CHECK(!d.dio());
        d.f.write(true, static_cast<uint8_t>(i ^ 0x5A));
    }
    uint8_t r[7];
    d.result7(r);
    CHECK_EQ(r[0], 0x40);
    CHECK_EQ(r[1], 0x80);
    CHECK_EQ(r[5], 6);
    CHECK_EQ(disk->writes(), w0 + 1);
    CHECK_EQ(disk->sector(0, 1, 5)[3], 3 ^ 0x5A);

    disk->setWriteProtected(true);
    d.cmd({0x45, 0x00, 0, 1, 6, 2, 6, 0x1B, 0xFF});
    CHECK(!d.exm());
    d.result7(r);
    CHECK_EQ(r[0], 0x40);
    CHECK_EQ(r[1] & 0x02, 0x02);
    CHECK_EQ(disk->sector(0, 1, 6)[3], 0);
    CHECK_EQ(disk->writes(), w0 + 1);
}

TEST(fdc_missing_sector) {
    auto disk = Disk::formatted(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x46, 0x00, 0, 0, 12, 2, 12, 0x1B, 0xFF});
    uint8_t r[7];
    d.result7(r);
    CHECK_EQ(r[0], 0x40);
    CHECK_EQ(r[1], 0x04);
}

TEST(fdc_unformatted_track) {
    auto disk = Disk::blank(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x46, 0x00, 0, 0, 1, 2, 1, 0x1B, 0xFF});
    uint8_t r[7];
    d.result7(r);
    CHECK_EQ(r[0], 0x40);
    CHECK_EQ(r[1], 0x01);
}

TEST(fdc_format_track) {
    auto disk = Disk::blank(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x0F, 0x00, 3});  // seek to cylinder 3
    d.cmd({0x08});
    d.f.read(true); d.f.read(true);
    d.cmd({0x4D, 0x00, 0x02, 0x0A, 0x1B, 0xE5});
    for (int i = 0; i < 10; ++i) {
        for (int b : {3, 0, i, 2}) {
            CHECK(d.exm());
            CHECK(!d.dio());
            d.f.write(true, static_cast<uint8_t>(b));
        }
    }
    uint8_t r[7];
    d.result7(r);
    CHECK_EQ(r[0], 0x00);
    CHECK(disk->trackFormatted(3, 0));
    CHECK(!disk->trackFormatted(3, 1));
    CHECK_EQ(disk->sector(3, 0, 9)[511], 0xE5);
    d.cmd({0x46, 0x00, 3, 0, 9, 2, 9, 0x1B, 0xFF});
    int n = 0;
    while (d.exm()) { CHECK_EQ(d.f.read(true), 0xE5); ++n; }
    CHECK_EQ(n, 512);
    d.result7(r);
    CHECK_EQ(r[0], 0x40);
    CHECK_EQ(r[1], 0x80);
}

TEST(fdc_read_id) {
    auto disk = Disk::formatted(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x4A, 0x00});
    uint8_t r[7];
    d.result7(r);
    CHECK_EQ(r[0], 0x00);
    CHECK_EQ(r[3], 0);
    CHECK_EQ(r[4], 0);
    CHECK(r[5] < 10);
    CHECK_EQ(r[6], 2);
}

TEST(fdc_wrong_rate) {
    auto disk = Disk::formatted(Density::High);
    Drv d;
    d.f.setDisk(disk.get());
    d.f.setHighRate(false);
    uint8_t r[7];
    d.cmd({0x46, 0x00, 0, 0, 1, 2, 1, 0x1B, 0xFF});
    d.result7(r);
    CHECK_EQ(r[0], 0x40);
    CHECK_EQ(r[1], 0x01);
    d.cmd({0x4A, 0x00});
    d.result7(r);
    CHECK_EQ(r[0], 0x40);
    CHECK_EQ(r[1], 0x01);
    d.f.setHighRate(true);
    d.cmd({0x4A, 0x00});
    d.result7(r);
    CHECK_EQ(r[0], 0x00);
}

TEST(fdc_eject_mid_read) {
    auto disk = Disk::formatted(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x46, 0x00, 0, 0, 1, 2, 1, 0x1B, 0xFF});
    for (int i = 0; i < 100; ++i) d.f.read(true);
    d.f.setDisk(nullptr);
    CHECK(!d.exm());
    uint8_t r[7];
    d.result7(r);
    CHECK_EQ(r[0], 0x48);
    CHECK_EQ(r[5], 1);
    d.f.read(true);  // safe
    d.cmd({0x04, 0x00});
    CHECK_EQ(d.f.read(true), 0x00);
}

TEST(fdc_swap_mid_write) {
    auto a = Disk::formatted(Density::Double);
    auto b = Disk::formatted(Density::Double);
    Drv d;
    d.f.setDisk(a.get());
    d.cmd({0x45, 0x00, 0, 0, 1, 2, 1, 0x1B, 0xFF});
    for (int i = 0; i < 100; ++i) d.f.write(true, 0xEE);
    d.f.setDisk(b.get());
    CHECK(!d.exm());
    for (int i = 0; i < 600; ++i) d.f.write(true, 0xEE);  // ignored as data
    CHECK_EQ(a->sector(0, 0, 1)[100], 0);
    CHECK_EQ(a->sector(0, 0, 1)[99], 0xEE);
    CHECK_EQ(b->sector(0, 0, 1)[0], 0);
    CHECK_EQ(b->writes(), 0u);
}

TEST(fdc_reset_mid_read) {
    auto disk = Disk::formatted(Density::Double);
    Drv d;
    d.f.setDisk(disk.get());
    d.cmd({0x46, 0x00, 0, 0, 1, 2, 1, 0x1B, 0xFF});
    d.f.read(true);
    d.f.reset();
    CHECK_EQ(d.f.read(false), 0x80);
    d.cmd({0x04, 0x00});
    CHECK_EQ(d.f.read(true), 0x20 | 0x08 | 0x10);
}

// The 72069's own register (A0 = 0 writes, T24): 0x36 is the software reset, 0x88 / 0xC8 the DD / HD rate.
TEST(fdc_control_reset_and_rate) {
    auto disk = Disk::formatted(Density::High);
    Drv d;
    d.f.setDisk(disk.get());
    uint8_t r[7];
    d.f.write(false, 0xC8);
    d.f.write(false, 0xE3);
    d.cmd({0x46, 0x00, 0, 0, 1, 2, 1, 0x1B, 0xFF});
    CHECK(d.exm());
    d.f.read(true);
    d.f.write(false, 0x36);
    CHECK_EQ(d.f.control(), 0x36);
    CHECK_EQ(d.f.read(false), 0x80);
    d.f.write(false, 0x88);
    d.cmd({0x46, 0x00, 0, 0, 1, 2, 1, 0x1B, 0xFF});
    d.result7(r);
    CHECK_EQ(r[1], 0x01);  // DD rate, HD disk: no address mark
    d.f.write(false, 0x1E);  // motor bits leave the rate alone
    d.f.write(false, 0x36);  // and so does a reset
    d.cmd({0x46, 0x00, 0, 0, 1, 2, 1, 0x1B, 0xFF});
    d.result7(r);
    CHECK_EQ(r[1], 0x01);
}
