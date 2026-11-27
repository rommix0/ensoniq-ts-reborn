#include <algorithm>
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
    a->formatTrack(0, 0, 0);
    CHECK(a->copyInto(*b));
    CHECK_EQ(b->sector(3, 1, 4)[7], 0x42);
    CHECK_EQ(b->writes(), a->writes());  // A119: the app's "modified" baseline after a save
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
    f[0x4E] = 0x0D;
    f[0x4F] = 0x0A;
    f[0x9D] = 0x0D;
    f[0x9E] = 0x0A;
    f[0x9F] = 0x1A;
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

namespace {
// A valid DD Giebler file with one stored block, for the negative tests.
std::vector<uint8_t> goodGiebler() {
    std::vector<uint8_t> f(512, ' ');
    f[0] = 0x0D; f[1] = 0x0A; f[0x4E] = 0x0D; f[0x4F] = 0x0A;
    f[0x9D] = 0x0D; f[0x9E] = 0x0A; f[0x9F] = 0x1A;
    std::fill(f.begin() + 0xA0, f.begin() + 0xA0 + 200, uint8_t(0xFF));
    f[0xA0] = 0x7F;
    f[0x1FF] = 7;
    f.insert(f.end(), 512, 0x11);
    f.push_back(0x1A);
    return f;
}
}  // namespace

TEST(image_giebler_refuses_damaged_files) {
    DecodedImage out;
    std::string err;
    CHECK(decodeImage(goodGiebler(), out, err));
    // A text file that happens to start with CRLF.
    std::vector<uint8_t> text(2000, 'a');
    text[0] = 0x0D;
    text[1] = 0x0A;
    CHECK(!decodeImage(text, out, err));
    auto f = goodGiebler();
    f.resize(512);
    CHECK(!decodeImage(f, out, err));  // header only, no trailing 1A
    f = goodGiebler();
    f.resize(100);
    CHECK(!decodeImage(f, out, err));  // header cut short
    f = goodGiebler();
    f[0xA1] = 0x7F;  // bitmap now lists two blocks, the file holds one
    CHECK(!decodeImage(f, out, err));
    f = goodGiebler();
    f.erase(f.end() - 100, f.end());  // truncated body
    CHECK(!decodeImage(f, out, err));
    f = goodGiebler();
    f.push_back(0);  // trailing bytes
    CHECK(!decodeImage(f, out, err));
    f = goodGiebler();
    f[0x4E] = 0;  // marker bytes missing
    CHECK(!decodeImage(f, out, err));
    f = goodGiebler();
    f[0x9F] = 0;
    CHECK(!decodeImage(f, out, err));
}

TEST(image_gkh_fresh_header_bytes) {
    // T21: the header of a new .gkh file, from the real 58-byte layout; only 0x18 and 0x1E..0x21 differ by density.
    static const uint8_t dd[58] = {
        'T', 'D', 'D', 'F', 'I', 0x01, 0x05, 0x00, 0x01, 0x04, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x0A, 0x05, 0x50, 0x00, 0x02, 0x00, 0x0A, 0x00, 0x00, 0x02, 0x0B, 0x0B, 0x00, 0x80,
        0x0C, 0x00, 0x3A, 0x00, 0x00, 0x00, 0x15, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x14, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::vector<uint8_t> want(dd, dd + 58);
    auto head = [](Density d) {
        auto g = encodeImage(*Disk::formatted(d), ImageFormat::Gkh, {});
        return std::vector<uint8_t>(g.begin(), g.begin() + 58);
    };
    CHECK(head(Density::Double) == want);
    want[0x18] = 0x14;
    want[0x1E] = 0x00; want[0x1F] = 0x00; want[0x20] = 0x19; want[0x21] = 0x00;
    CHECK(head(Density::High) == want);
    // And the result decodes as a GKH file.
    DecodedImage out;
    std::string err;
    CHECK(decodeImage(encodeImage(*Disk::formatted(Density::High), ImageFormat::Gkh, {}), out, err));
    CHECK(out.format == ImageFormat::Gkh && out.disk->density() == Density::High);
}

TEST(image_format_for_name) {
    CHECK(formatForName("a.GKH") == ImageFormat::Gkh);
    CHECK(formatForName("b.eda") == ImageFormat::Giebler);
    CHECK(formatForName("c.edt") == ImageFormat::Giebler);
    CHECK(formatForName("x.EDE") == ImageFormat::Giebler);
    CHECK(formatForName("d.img") == ImageFormat::Raw);
    CHECK(formatForName("e") == ImageFormat::Raw);
    CHECK(formatForName("f.HFE") == ImageFormat::Hfe);
}

namespace {
bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
bool sameSectors(const Disk& a, const Disk& b) {
    if (a.cylinders() != b.cylinders() || a.density() != b.density()) return false;
    for (int c = 0; c < a.cylinders(); ++c)
        for (int h = 0; h < Disk::kHeads; ++h) {
            if (a.trackFormatted(c, h) != b.trackFormatted(c, h)) return false;
            for (int s = 0; s < a.sectorsPerTrack() && a.trackFormatted(c, h); ++s)
                if (std::memcmp(a.sector(c, h, s), b.sector(c, h, s), Disk::kSectorBytes) != 0) return false;
        }
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
        // A fresh header matches the real file's (T21).
        const auto fresh = encodeImage(*da.disk, ImageFormat::Gkh, {});
        CHECK(std::equal(fresh.begin(), fresh.begin() + 58, fa.begin()));
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

TEST(disk_marks_reads) {
    auto a = Disk::formatted(Density::Double);
    a->noteRead(5);
    a->noteRead(5);
    CHECK_EQ(a->reads(), 1u);
    CHECK(a->wasRead(5));
    CHECK(!a->wasRead(6));
    auto b = Disk::blank(Density::Double);
    CHECK(a->copyInto(*b));
    CHECK_EQ(b->reads(), 1u);
    CHECK(b->wasRead(5));
    CHECK(!b->wasRead(6));
    b->clearReads();
    CHECK_EQ(b->reads(), 0u);
    CHECK(!b->wasRead(5));
    CHECK(a->wasRead(5));
}

// DECISION T30: a disk may have more cylinders than 80 (an HxC "super disk"); the block order runs on past 80.
TEST(disk_more_cylinders) {
    auto d = Disk::formatted(Density::High, 255);
    CHECK_EQ(d->cylinders(), 255);
    CHECK_EQ(d->bytes(), size_t(255) * 2 * 20 * 512);
    CHECK(d->sector(254, 1, 19) != nullptr);
    CHECK(d->sector(255, 0, 0) == nullptr);
    CHECK(d->block(10199) == d->sector(254, 1, 19));
    auto small = Disk::blank(Density::High);
    CHECK_EQ(small->cylinders(), 80);
    CHECK(!d->copyInto(*small));  // the cylinders must agree, as the density
    auto same = Disk::blank(Density::High, 255);
    CHECK(d->copyInto(*same));
}

// DECISION T30: HFE (HxC v1, MFM) written and read back: same density, cylinders, formatted tracks and sectors.
TEST(image_hfe_roundtrip) {
    for (const auto& [den, cyls] : {std::pair{Density::Double, 80}, std::pair{Density::High, 255}}) {
        auto d = Disk::formatted(den, cyls);
        const int blocks = cyls * 2 * d->sectorsPerTrack();
        for (int n = 0; n < blocks; ++n)
            for (int i = 0; i < Disk::kSectorBytes; ++i) d->block(n)[i] = uint8_t(n * 7 + i);
        const auto file = encodeImage(*d, ImageFormat::Hfe, {});
        CHECK(file.size() > 512 && std::memcmp(file.data(), "HXCPICFE", 8) == 0);
        DecodedImage back;
        std::string err;
        CHECK(decodeImage(file, back, err));
        CHECK(back.format == ImageFormat::Hfe);
        CHECK(back.disk && sameSectors(*d, *back.disk));
        CHECK(encodeImage(*back.disk, ImageFormat::Hfe, back.header) == file);
    }
    auto b = Disk::blank(Density::Double);
    b->formatTrack(0, 0, 0xE5);
    DecodedImage back;
    std::string err;
    CHECK(decodeImage(encodeImage(*b, ImageFormat::Hfe, {}), back, err));
    CHECK(back.disk && back.disk->trackFormatted(0, 0) && !back.disk->trackFormatted(0, 1));
    CHECK(back.disk && back.disk->sector(0, 0, 9)[511] == 0xE5);
}

// DECISION T30: only HFE v1 with MFM is read; others are named in the error.
TEST(image_hfe_refuses_other_kinds) {
    auto file = encodeImage(*Disk::formatted(Density::Double), ImageFormat::Hfe, {});
    DecodedImage out;
    std::string err;
    file[11] = 2;  // track encoding: Amiga MFM
    CHECK(!decodeImage(file, out, err));
    CHECK(err.find("HFE") != std::string::npos);
    std::memcpy(file.data(), "HXCHFEV3", 8);
    CHECK(!decodeImage(file, out, err));
    CHECK(err.find("HFE") != std::string::npos);
    file.resize(300);
    std::memcpy(file.data(), "HXCPICFE", 8);
    CHECK(!decodeImage(file, out, err));
}

// DECISION T30: the user's HxC files hold the same sectors as the same disks in GKH and IMG form. 84-track
// files with blank tracks 80-83 are 80-cylinder disks.
TEST(image_hfe_user_files) {
    const std::string dir = ENS_DISK_DIR;
    std::vector<uint8_t> probe;
    const std::string eps = dir + "/EPS & EPS-16/Factory Sounds";
    if (dir.empty() || !readFile(eps + "/EPS-orchestral/Orchestral/ORCH1.GKH", probe)) {
        std::printf("  skip: disk images not found\n");
        return;
    }
    const std::string ed = "/Ensoniq EPS16 Factory Floppy Archive (15 disks, fixed) [IMG & GKH]/"
                           "Ensoniq EPS16 Factory Floppy Archive (15 disks) [IMG & GKH]/IMG/ED-001.IMG";
    struct Pair { std::string hfe, other; };
    const Pair pairs[] = {
        {eps + "/EPS-orchestral/Orchestral/HFE/ORCH1_GKH.hfe", eps + "/EPS-orchestral/Orchestral/ORCH1.GKH"},
        {eps + "/Ensoniq EPS16 Factory Floppy Archive (for ASR10) [HxC type HFE]/ED-001_ASR10.hfe", eps + ed},
    };
    for (const Pair& p : pairs) {
        std::vector<uint8_t> fa, fb;
        CHECK(readFile(p.hfe, fa));
        CHECK(readFile(p.other, fb));
        DecodedImage da, db;
        std::string err;
        CHECK(decodeImage(fa, da, err));
        CHECK(decodeImage(fb, db, err));
        CHECK(da.format == ImageFormat::Hfe);
        CHECK(da.disk && db.disk && sameSectors(*da.disk, *db.disk));
    }
    std::vector<uint8_t> f84;
    CHECK(readFile(eps + "/EPS-Percussion/DrumSets/HFE/DRMSET01_GKH.hfe", f84));
    CHECK(f84.size() > 9 && f84[9] == 84);
    DecodedImage d84;
    std::string err;
    CHECK(decodeImage(f84, d84, err));
    CHECK(d84.disk && d84.disk->cylinders() == 80 && d84.disk->density() == Density::Double);
}
