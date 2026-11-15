// The OS v3.10 with a disk in the drive (T23, T24): the user's ED-001 (EPS-16 PLUS, DD, GKH) and AD-001
// (ASR-10, HD, raw) images from ENS_DISK_DIR. Skipped without the ROMs or the images.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "files.h"
#include "ts/disk_image.h"
#include "ts_firmware.h"

using namespace ens::ts;

namespace {

const char* const kEd001 =
    "/EPS & EPS-16/Factory Sounds/Ensoniq EPS16 Factory Floppy Archive (15 disks, fixed) [IMG & GKH]/"
    "Ensoniq EPS16 Factory Floppy Archive (15 disks) [IMG & GKH]/GKH/ED-001.GKH";
const char* const kAd001 =
    "/ASR-10/Factory Sounds/Ensoniq ASR10 - Essential Sound Disks (Factory Library) [IMG, GKH, & EDA]/AD-001.img";

// The image under ENS_DISK_DIR; nullptr with a skip line when it is missing.
std::unique_ptr<Disk> loadDisk(const char* rel) {
    std::vector<uint8_t> file;
    std::string err;
    const std::string dir = ENS_DISK_DIR;
    if (dir.empty() || !readFile(dir + rel, file, err)) {
        std::printf("  skip: disk images not found\n");
        return nullptr;
    }
    DecodedImage img;
    CHECK(decodeImage(file, img, err));
    return std::move(img.disk);
}

// The name of the first sampled sound (file type 3) in the directory: 26-byte entries from block 3, the type
// at byte 1, the name at bytes 2-13 (Giebler, "Ensoniq Floppy Diskette Format"). Trailing spaces cut.
std::string firstSoundName(Disk& d) {
    std::vector<uint8_t> dir(d.block(3), d.block(3) + Disk::kSectorBytes);
    dir.insert(dir.end(), d.block(4), d.block(4) + Disk::kSectorBytes);
    for (size_t e = 0; e + 26 <= dir.size(); e += 26)
        if (dir[e + 1] == 3) {
            std::string name(dir.begin() + e + 2, dir.begin() + e + 14);
            return name.substr(0, name.find_last_not_of(' ') + 1);
        }
    return "";
}

// A TS booted with the disk in from power-on (as the hunt did); nullptr without ROMs or the disk.
std::unique_ptr<Machine> bootWith(std::unique_ptr<Disk> d) {
    if (!d) return nullptr;
    auto m = loadTs();
    if (!m) return nullptr;
    CHECK(m->insertDisk(std::move(d)) == nullptr);
    runMs(*m, kBootMs);
    return m;
}

// Storage twice: the Disk Load page, which reads the disk's directory (Musician's Manual, Section 13).
void openDiskLoad(Machine& m) {
    tap(m, Button::Storage);
    m.button(Button::Storage, true);
    runMs(m, 60);
    m.button(Button::Storage, false);
}

struct Seen {
    bool text = false, notReady = false, notResponding = false;
};

// Runs ms, or until `text` shows (when stopAtText); what the display showed on any frame.
Seen watch(Machine& m, uint32_t ms, const std::string& text, bool stopAtText = true) {
    Seen s;
    const uint64_t end = m.cycles() + static_cast<uint64_t>(ms) * kCpuHz / 1000;
    Audio a;
    while (m.cycles() < end && !(stopAtText && s.text)) {
        m.runFrame(a);
        s.text = s.text || shows(m.panel(), text.c_str());
        s.notReady = s.notReady || shows(m.panel(), "NOT READY");
        s.notResponding = s.notResponding || shows(m.panel(), "NOT RESPONDING");
    }
    return s;
}

}  // namespace

// The Disk Load page lists the disk's directory: its first sampled sound shows as LOAD FILE=<name>.
TEST(ts_disk_load_page_lists_directory) {
    auto disk = loadDisk(kEd001);
    if (!disk) return;
    const std::string name = "FILE=" + firstSoundName(*disk);
    auto m = bootWith(std::move(disk));
    if (!m) return;
    openDiskLoad(*m);
    const Seen s = watch(*m, 3000, name);
    std::printf("  |%s|\n", rowText(m->panel(), 0).c_str());
    CHECK(s.text);
    CHECK(!s.notReady);
    CHECK(!s.notResponding);
    CHECK(m->disk() != nullptr);
}

// The disk taken out while the OS reads a sector (data moving: MSR 0xF0): the OS's DISK DRIVE NOT RESPONDING
// (the read ends with ST0 NR, T22), then its "PRESS ANY BUTTON TO CONTINUE..."; a button brings back the
// Storage page.
TEST(ts_eject_mid_read_reports_error) {
    auto m = bootWith(loadDisk(kEd001));
    if (!m) return;
    bool reading = false;
    m->onBusEvent = [&reading](const BusEvent& e) {
        reading = reading || (e.dev == Device::Fdc && !e.write && e.addr == 0x280001 && e.value == 0xF0);
    };
    openDiskLoad(*m);
    const uint64_t end = m->cycles() + static_cast<uint64_t>(3000) * kCpuHz / 1000;
    Audio a;
    while (!reading && m->cycles() < end) m->runFrame(a);
    m->onBusEvent = nullptr;
    CHECK(reading);
    CHECK(m->insertDisk(nullptr) != nullptr);
    CHECK(m->disk() == nullptr);
    CHECK(watch(*m, 3000, "PRESS ANY BUTTON").notResponding);
    tap(*m, Button::Up);
    const Seen back = watch(*m, 3000, "SAVE");
    std::printf("  |%s|\n", rowText(m->panel(), 1).c_str());
    CHECK(back.text);
    CHECK(shows(m->panel(), "LOAD"));
}

// A DD disk, then an HD one swapped in: the OS sees the change (IP1), reads DENS (IP2) and picks the rate
// (control 0x88 / 0xC8), and lists each directory.
TEST(ts_reads_both_densities) {
    auto hd = loadDisk(kAd001);
    auto dd = loadDisk(kEd001);
    if (!hd || !dd) return;
    const std::string ddName = "FILE=" + firstSoundName(*dd), hdName = "FILE=" + firstSoundName(*hd);
    CHECK(ddName != hdName);
    auto m = bootWith(std::move(dd));
    if (!m) return;
    openDiskLoad(*m);
    CHECK(watch(*m, 3000, ddName).text);
    runMs(*m, 500);
    auto out = m->insertDisk(std::move(hd));
    CHECK(out != nullptr && out->density() == Density::Double);
    CHECK(m->disk()->density() == Density::High);
    runMs(*m, 500);
    openDiskLoad(*m);
    const Seen s = watch(*m, 3000, hdName);
    std::printf("  |%s|\n", rowText(m->panel(), 0).c_str());
    CHECK(s.text);
    CHECK(!s.notReady);
    CHECK(!s.notResponding);
}

// Ejected after a directory was read, the drive is empty again (T10): DISK DRIVE NOT READY.
TEST(ts_empty_drive_still_reports_no_disk) {
    auto disk = loadDisk(kEd001);
    if (!disk) return;
    const std::string name = "FILE=" + firstSoundName(*disk);
    auto m = bootWith(std::move(disk));
    if (!m) return;
    openDiskLoad(*m);
    CHECK(watch(*m, 3000, name).text);
    runMs(*m, 500);
    CHECK(m->insertDisk(nullptr) != nullptr);
    CHECK(m->insertDisk(nullptr) == nullptr);
    openDiskLoad(*m);
    const Seen s = watch(*m, 3000, "DISK DRIVE NOT READY");
    CHECK(s.text);
    CHECK(!s.notResponding);
}
