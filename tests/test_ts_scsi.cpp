// The OS v3.10 with the SP-4 SCSI board fitted (T25-T29): the probe and reset at boot, the SCSI STATUS page, and a
// CD-ROM at ID 4 served from the user's CD images under ENS_DISK_DIR. Skipped without the ROMs or the images.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "ts_disks.h"
#include "ts_firmware.h"

using namespace ens::ts;
using namespace ts_disks;

// The probe (c0f820) writes 0x55, 0xAA and 0xFF to register 2 and reads each back equal; on the match (c0f7d4) the
// OS issues Reset (0x00 to the Command register, 0x18). The OS then boots to the play screen and the STORAGE page
// shows SCSI on its bottom row (service manual p. 53, step 14).
TEST(ts_scsi_probe_and_reset) {
    auto m = loadTs(false, true);
    if (!m) return;
    CHECK(m->scsiFitted());
    uint8_t addr = 0, lastWritten = 0;
    bool written[3] = {}, readBack[3] = {}, reset = false;
    const uint8_t probe[3] = {0x55, 0xAA, 0xFF};
    m->onBusEvent = [&](const BusEvent& e) {
        if (e.dev != Device::Scsi) return;
        const bool a0 = (e.addr & 2) != 0;  // chip A0 = CPU A1 (T25)
        const uint8_t v = static_cast<uint8_t>(e.value);
        if (!a0) {
            if (e.write) addr = v & 0x1F;
            return;
        }
        if (addr == 2)
            for (int i = 0; i < 3; ++i) {
                if (e.write && v == probe[i]) written[i] = true;
                if (!e.write && written[i] && v == probe[i] && lastWritten == probe[i]) readBack[i] = true;
            }
        if (e.write && addr == 2) lastWritten = v;
        if (e.write && addr == 0x18 && v == 0x00) reset = true;
        if (addr != 0x18 && addr != 0x19) addr = static_cast<uint8_t>(addr + 1);  // datasheet 3.1.2
    };
    runMs(*m, kBootMs);
    m->onBusEvent = nullptr;
    for (int i = 0; i < 3; ++i) CHECK(written[i] && readBack[i]);
    CHECK(reset);
    printRows(*m);
    CHECK(rowText(m->panel(), 0) == "U0-0 BABY-GRAND   FULL-BODY   BRITE-PNO ");
    tap(*m, Button::Storage);
    printRows(*m);
    CHECK(rowText(m->panel(), 1).find("SCSI") != std::string::npos);
}

// Without the board the SCSI block reads 0 as before (T12): the boot is bit-identical to Plan 3's.
TEST(ts_scsi_absent_is_unchanged) {
    auto m = loadTs();
    if (!m) return;
    CHECK(!m->scsiFitted());
    CHECK(m->cdPort() == nullptr);
    CHECK(m->cd() == nullptr);
    const uint64_t h = traceHash(*m, kBootMs);
    std::printf("  boot trace hash %016llx\n", static_cast<unsigned long long>(h));
    CHECK_EQ(h, 0x3bfc7336098a6defull);
}

// CDR-13 (InVision Percussion) at ID 4: the SCSI STATUS page shows its volume name, CDR-013 (as observed).
TEST(ts_cd_status_shows_volume) {
    ServedCd cd;
    if (!openCd(kCdr13, cd)) return;
    auto m = bootScsi(&cd);
    if (!m) return;
    openScsiStatus(*m, &cd);
    printRows(*m);
    CHECK(shows(m->panel(), "DEVICE-ID=4"));
    CHECK(shows(m->panel(), "STATUS=CDR-013"));
    CHECK(!shows(m->panel(), "INVALID"));
    CHECK(!shows(m->panel(), "FOREIGN"));
    CHECK(!shows(m->panel(), "STANDBY"));
}

// The blocks free in sample RAM on a TS with no sampled sounds (FREE=4018 on the bank-select page, as observed).
constexpr int kFreeBlocks = 4018;

// The SIZE=<blocks> on the Load page's top row; -1 when none shows.
int shownSize(const Machine& m) {
    const std::string row = rowText(m.panel(), 0);
    const size_t at = row.find("SIZE=");
    return at == std::string::npos ? -1 : std::atoi(row.c_str() + at + 5);
}

// From *LOAD* (Soft4) on the SCSI STATUS page: the root lists directories; *YES* (Soft3) walks down each first
// directory to a list of sampled sounds; Soft1 (the FILE field) and Up step to the first one that fits sample RAM, which loads into S8-0 (as
// withFirstSound in test_ts_floppy.cpp), is selected and plays C-E-G from sample RAM. On CDR-13: ROOT, PERCUSSION,
// PERC KITS, whose first sound is larger than the free blocks (ETHNIC PERC1, SIZE=4201: the OS shows NOT ENOUGH
// ROOM, SHORTAGE=183); the next, ETHNIC PERC2 (SIZE=3927), fits.
TEST(ts_cd_loads_and_plays) {
    ServedCd cd;
    if (!openCd(kCdr13, cd)) return;
    auto m = bootScsi(&cd);
    if (!m) return;
    openScsiStatus(*m, &cd);
    press(*m, &cd, Button::Soft4);  // *LOAD*
    CHECK(until(*m, &cd, "TYPE=", 5000));
    printRows(*m);
    for (int depth = 0; depth < 4 && shows(m->panel(), "TYPE=DIRECTORY"); ++depth) {
        press(*m, &cd, Button::Soft3);  // *YES*: into the directory
        until(*m, &cd, "LOAD FILE=", 5000);
        runServed(*m, &cd, 500);
        printRows(*m);
    }
    CHECK(shows(m->panel(), "TYPE=SAMPLEDSND"));
    press(*m, &cd, Button::Soft1);  // the FILE field (the cursor starts on TYPE, where Up/Down change the type)
    for (int i = 0; i < 38 && shownSize(*m) > kFreeBlocks; ++i) press(*m, &cd, Button::Up);  // 38 files a directory
    printRows(*m);
    CHECK(shownSize(*m) > 0 && shownSize(*m) <= kFreeBlocks);
    const std::string row = rowText(m->panel(), 0);
    const size_t at = row.find("FILE=");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    std::string name = row.substr(at + 5, 12);
    name = name.substr(0, name.find_last_not_of(' ') + 1);
    press(*m, &cd, Button::Soft3);  // *YES*: USE BANK BUTTONS TO SELECT LOCATION, BANKSET=8
    CHECK(shows(m->panel(), "BANKSET=8"));
    m->button(Button::Bank0, true);
    runServed(*m, &cd, 60);
    m->button(Button::Bank0, false);
    CHECK(until(*m, &cd, "LOADING " + name, 2000));
    CHECK(until(*m, &cd, "LOAD FILE=", 60000));  // back on the Load page when the sound is in
    printRows(*m);

    press(*m, &cd, Button::Sounds);
    m->button(Button::BankSet, true);  // BankSet held, Bank 8: BankSet S8
    runServed(*m, &cd, 60);
    press(*m, &cd, Button::Bank8);
    m->button(Button::BankSet, false);
    runServed(*m, &cd, 300);
    printRows(*m);
    CHECK(shows(m->panel(), "S8-0 SAMPLED-SOUND"));
    CHECK(shows(m->panel(), name.c_str()));
    press(*m, &cd, Button::Soft5);  // the sound's name: select it
    SampleVoices v;
    m->onBusEvent = [&v](const BusEvent& e) { v(e); };
    for (int k : {60, 64, 67}) m->key(k, true, 127);
    int peak = 0;
    const uint64_t end = m->cycles() + static_cast<uint64_t>(400) * kCpuHz / 1000;
    Audio a;
    while (m->cycles() < end) {
        m->runFrame(a);
        cd.serve(*m);
        peak = std::max({peak, std::abs(int{a.mainL}), std::abs(int{a.mainR})});
    }
    m->onBusEvent = nullptr;
    std::printf("  %s: peak %d, voices from sample RAM %d, emulated %.1f s\n", name.c_str(), peak, v.count,
                static_cast<double>(m->cycles()) / kCpuHz);
    CHECK(v.count >= 3);
    CHECK(peak > 1000);
}

// SCSI fitted, the drive at ID 4 empty: the manual allows STANDBY (a removable media device with no volume) or
// INVALID (no device). Observed: STANDBY. TEST UNIT READY answers CHECK CONDITION, REQUEST SENSE gives NOT READY,
// MEDIUM NOT PRESENT (2/0x3A, T28); INVALID is what a selection timeout (no device at the ID) gives.
TEST(ts_cd_empty_drive) {
    auto m = bootScsi(nullptr);
    if (!m) return;
    CHECK(m->cd() != nullptr && !m->cd()->hasDisc());
    openScsiStatus(*m, nullptr);
    printRows(*m);
    CHECK(shows(m->panel(), "DEVICE-ID=4"));
    CHECK(shows(m->panel(), "STATUS=STANDBY"));
}
