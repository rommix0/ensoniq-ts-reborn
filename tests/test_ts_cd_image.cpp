// CD image layouts (ts/cd_image.h) and the tools-side reader (cd_file.h). The user-CD test needs ENS_DISK_DIR.
#include <cstdio>
#include <string>

#include "cd_file.h"
#include "check.h"
#include "ts/cd_image.h"

using namespace ens::ts;

TEST(cd_cue_mode1_2352) {
    CdLayout l;
    std::string err;
    CHECK(parseCue("FILE \"a b.bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n", l, err));
    CHECK(l.file == "a b.bin");
    CHECK_EQ(l.stride, 2352u);
    CHECK_EQ(l.skip, 16u);
    CHECK_EQ(sectorOffset(l, 3), (uint64_t)3 * 2352 + 16);
}

TEST(cd_cue_refuses_other_layouts) {
    const char* bad[] = {
        "FILE \"a.bin\" BINARY\n TRACK 01 MODE2/2352\n  INDEX 01 00:00:00\n",
        "FILE \"a.bin\" BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n",
        "FILE \"a.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n TRACK 02 MODE1/2352\n  INDEX 01 00:01:00\n",
        "FILE \"a.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:02:00\n",
        "TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n",
    };
    for (const char* t : bad) {
        CdLayout l;
        std::string err;
        CHECK(!parseCue(t, l, err));
        CHECK(err.find("MODE1/2352") != std::string::npos);
    }
}

TEST(cd_iso_count) {
    CdLayout l = isoLayout("x.iso");
    CHECK_EQ(l.stride, 2048u);
    CHECK_EQ(l.skip, 0u);
    uint32_t n = 0;
    std::string err;
    CHECK(sectorCount(l, 263069696ull, n, err));
    CHECK_EQ(n, 128452u);
    CHECK(!sectorCount(l, 2049ull, n, err));
}

TEST(cd_bin_count) {
    CdLayout l;
    std::string err;
    CHECK(parseCue("FILE x.bin BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n", l, err));
    uint32_t n = 0;
    CHECK(sectorCount(l, 120779904ull, n, err));
    CHECK_EQ(n, 51352u);
    CHECK(!sectorCount(l, 2353ull, n, err));
}

TEST(cd_names) {
    CHECK(baseName("E:\\ENSONIQ-CDR-13-PERCUSSION-INVISION\\DLM-CDR13.BIN") == "DLM-CDR13.BIN");
    CHECK(sameNameNoCase("DLM-CDR13.BIN", "dlm-cdr13.bin"));
    CHECK(!sameNameNoCase("a.bin", "b.bin"));
}

TEST(cd_file_reads_user_cds) {
    const std::string dir = ENS_DISK_DIR;
    struct Cd { const char* path; uint32_t sectors; };
    const Cd cds[] = {
        {"/CD-ROMs/Ensoniq.CDR-13.InVision.Percussion.ENSONiQ-DELiRiUM/dlm-cdr13.cue", 51352},
        {"/CD-ROMs/Ensoniq.CDR-15.Syntaur.L.A. RIOT.Volume.II.ENSONIQ-BSOUND/CDR-15_L.A.Riot1.iso", 128452},
    };
    CdFile probe;
    std::string err;
    if (dir.empty() || !probe.open(dir + cds[0].path, err)) {
        std::printf("  skip: disk images not found\n");
        return;
    }
    for (const Cd& c : cds) {
        CdFile f;
        CHECK(f.open(dir + c.path, err));
        CHECK_EQ(f.sectors(), c.sectors);
        uint8_t buf[2048];
        CHECK(f.read(0, buf));
    }
}
