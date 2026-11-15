#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "check.h"
#include "ts/disk_image.h"

using namespace ens::ts;

TEST(disk_geometry) {
    auto dd = Disk::formatted(Density::Double);
    auto hd = Disk::formatted(Density::High);
    CHECK_EQ(dd->sectorsPerTrack(), 10);
    CHECK_EQ(dd->bytes(), 819200u);
    CHECK_EQ(hd->sectorsPerTrack(), 20);
    CHECK_EQ(hd->bytes(), 1638400u);
    CHECK(dd->block(23) == dd->sector(1, 0, 3));
    CHECK(dd->sector(0, 0, 10) == nullptr);
    CHECK(hd->sector(0, 0, 19) != nullptr);
}

TEST(disk_blank_is_unformatted) {
    auto d = Disk::blank(Density::High);
    CHECK(d->sector(0, 0, 0) == nullptr);
    CHECK_EQ(d->writes(), 0u);
    d->formatTrack(0, 0, 0xE5);
    CHECK(d->sector(0, 0, 0) != nullptr);
    CHECK_EQ(d->sector(0, 0, 0)[0], 0xE5);
    CHECK_EQ(d->sector(0, 0, 19)[511], 0xE5);
    CHECK(d->sector(0, 1, 0) == nullptr);
    CHECK_EQ(d->writes(), 1u);
}

TEST(disk_copy_into_needs_same_density) {
    auto a = Disk::formatted(Density::Double);
    a->sector(3, 1, 4)[7] = 0x42;
    auto b = Disk::blank(Density::Double);
    CHECK(a->copyInto(*b));
    CHECK_EQ(b->sector(3, 1, 4)[7], 0x42);
    auto h = Disk::formatted(Density::High);
    h->sector(0, 0, 0)[0] = 9;
    CHECK(!a->copyInto(*h));
    CHECK_EQ(h->sector(0, 0, 0)[0], 9);
}

TEST(image_raw_sizes) {
    DecodedImage out;
    std::string err;
    CHECK(decodeImage(std::vector<uint8_t>(819200, 0), out, err));
    CHECK(out.disk->density() == Density::Double && out.format == ImageFormat::Raw);
    CHECK(decodeImage(std::vector<uint8_t>(1638400, 0), out, err));
    CHECK(out.disk->density() == Density::High);
    CHECK(!decodeImage(std::vector<uint8_t>(819201, 0), out, err));
    CHECK(err.find("819200") != std::string::npos && err.find("1638400") != std::string::npos);
}

TEST(image_gkh_roundtrip) {
    std::vector<uint8_t> f(58 + 819200, 0);
    std::memcpy(f.data(), "TDDFI", 5);
    for (size_t i = 58; i < f.size(); ++i) f[i] = uint8_t(i * 7);
    DecodedImage out;
    std::string err;
    CHECK(decodeImage(f, out, err));
    CHECK(out.format == ImageFormat::Gkh && out.disk->density() == Density::Double);
    CHECK_EQ(out.header.size(), 58u);
    CHECK_EQ(out.disk->sector(0, 0, 0)[1], f[59]);
    CHECK(encodeImage(*out.disk, ImageFormat::Gkh, out.header) == f);
}

TEST(image_giebler_omitted_block_written) {
    // Header with the DD bitmap: block 0 stored, every other block left out.
    std::vector<uint8_t> f(512, ' ');
    f[0] = 0x0D;
    f[1] = 0x0A;
    std::fill(f.begin() + 0xA0, f.end(), uint8_t(0));
    std::fill(f.begin() + 0xA0, f.begin() + 0xA0 + 200, uint8_t(0xFF));
    f[0xA0] = 0x7F;
    f[0x1FF] = 7;
    f.insert(f.end(), 512, 0x11);
    f.push_back(0x1A);
    DecodedImage out;
    std::string err;
    CHECK(decodeImage(f, out, err));
    CHECK(out.format == ImageFormat::Giebler && out.disk->density() == Density::Double);
    CHECK_EQ(out.disk->block(0)[0], 0x11);
    CHECK_EQ(out.disk->block(5)[0], 0);
    std::memset(out.disk->block(5), 0x55, 512);
    DecodedImage back;
    CHECK(decodeImage(encodeImage(*out.disk, ImageFormat::Giebler, out.header), back, err));
    CHECK_EQ(back.disk->block(5)[100], 0x55);
    CHECK_EQ(back.disk->block(0)[0], 0x11);
    CHECK_EQ(back.disk->block(6)[0], 0);
    // A new header (no kept one) also round-trips, for both densities.
    auto hd = Disk::formatted(Density::High);
    hd->block(3199)[511] = 0x99;
    DecodedImage hb;
    CHECK(decodeImage(encodeImage(*hd, ImageFormat::Giebler, {}), hb, err));
    CHECK(hb.disk->density() == Density::High);
    CHECK_EQ(hb.disk->block(3199)[511], 0x99);
}

TEST(image_format_for_name) {
    CHECK(formatForName("a.GKH") == ImageFormat::Gkh);
    CHECK(formatForName("b.eda") == ImageFormat::Giebler);
    CHECK(formatForName("c.edt") == ImageFormat::Giebler);
    CHECK(formatForName("x.EDE") == ImageFormat::Giebler);
    CHECK(formatForName("d.img") == ImageFormat::Raw);
    CHECK(formatForName("e") == ImageFormat::Raw);
}

namespace {
bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
bool sameSectors(const Disk& a, const Disk& b) {
    for (int c = 0; c < Disk::kCylinders; ++c)
        for (int h = 0; h < Disk::kHeads; ++h)
            for (int s = 0; s < a.sectorsPerTrack(); ++s)
                if (std::memcmp(a.sector(c, h, s), b.sector(c, h, s), Disk::kSectorBytes) != 0) return false;
    return true;
}
}  // namespace

TEST(image_user_files) {
    const std::string dir = ENS_DISK_DIR;
    std::vector<uint8_t> probe;
    if (dir.empty() || !readFile(dir + "/Tools/ede109/EPSBLANK.EDE", probe)) {
        std::printf("  skip: disk images not found\n");
        return;
    }
    struct Pair { const char* a; const char* b; Density d; };
    const Pair pairs[] = {
        {"/EPS & EPS-16/Factory Sounds/Ensoniq EPS16 Factory Floppy Archive (15 disks, fixed) [IMG & GKH]/"
         "Ensoniq EPS16 Factory Floppy Archive (15 disks) [IMG & GKH]/GKH/ED-001.GKH",
         "/EPS & EPS-16/Factory Sounds/Ensoniq EPS16 Factory Floppy Archive (15 disks, fixed) [IMG & GKH]/"
         "Ensoniq EPS16 Factory Floppy Archive (15 disks) [IMG & GKH]/IMG/ED-001.IMG",
         Density::Double},
        {"/ASR-10/Factory Sounds/Ensoniq ASR10 - Essential Sound Disks (Factory Library) [IMG, GKH, & EDA]/AD-001.GKH",
         "/ASR-10/Factory Sounds/Ensoniq ASR10 - Essential Sound Disks (Factory Library) [IMG, GKH, & EDA]/AD-001.img",
         Density::High},
    };
    for (const Pair& p : pairs) {
        std::vector<uint8_t> fa, fb;
        CHECK(readFile(dir + p.a, fa));
        CHECK(readFile(dir + p.b, fb));
        DecodedImage da, db;
        std::string err;
        CHECK(decodeImage(fa, da, err));
        CHECK(decodeImage(fb, db, err));
        CHECK(da.format == ImageFormat::Gkh && db.format == ImageFormat::Raw);
        CHECK(da.disk->density() == p.d && db.disk->density() == p.d);
        CHECK(sameSectors(*da.disk, *db.disk));
    }
    struct Blank { const char* name; Density d; };
    const Blank blanks[] = {{"EPSBLANK.EDE", Density::Double}, {"ASRBLANK.EDA", Density::High},
                            {"TS-DD.EDT", Density::Double}, {"TS-HD.EDT", Density::High}};
    for (const Blank& b : blanks) {
        std::vector<uint8_t> f;
        CHECK(readFile(dir + "/Tools/ede109/" + b.name, f));
        DecodedImage d;
        std::string err;
        CHECK(decodeImage(f, d, err));
        CHECK(d.format == ImageFormat::Giebler && d.disk->density() == b.d);
        CHECK(encodeImage(*d.disk, ImageFormat::Giebler, d.header) == f);
    }
}
