// The OS v3.10 with a disk in the drive (T23, T24): the user's ED-001 (EPS-16 PLUS, DD, GKH) and AD-001
// (ASR-10, HD, raw) images from ENS_DISK_DIR. Skipped without the ROMs or the images.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "app/engine.h"
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

namespace {

// A voice started from sample RAM: a CR write (pages 0x00-0x3F, register 0) with BS1:BS0 = 3, the bank the OS
// gives BankSet S8 (T4). Follows the page register (15) and CR's byte 2 (bits 15-8); byte 3 completes the write.
struct SampleVoices {
    int count = 0;
    unsigned page = 0, bs = 0;
    void operator()(const BusEvent& e) {
        if (e.dev != Device::Otto || !e.write) return;
        const unsigned ha = (e.addr >> 1) & 0x3F, reg = ha >> 2, byte = ha & 3;
        if (reg == 15 && byte == 3) page = e.value & 0x7F;
        if (reg != 0 || page >= 0x40) return;
        if (byte == 2) bs = (e.value >> 6) & 3;
        if (byte == 3 && bs == 3) ++count;
    }
};

// A machine booted with the disk that has loaded the disk's first sampled sound into S8-0 (Musician's Manual,
// Section 14, "Loading a Sampled Sound"); null without the ROMs or the image.
std::unique_ptr<Machine> withFirstSound(const char* rel, std::string& name) {
    auto disk = loadDisk(rel);
    if (!disk) return nullptr;
    name = firstSoundName(*disk);
    auto m = bootWith(std::move(disk));
    if (!m) return nullptr;
    openDiskLoad(*m);
    CHECK(watch(*m, 3000, "FILE=" + name).text);
    tap(*m, Button::Soft3);  // *YES*: USE BANK BUTTONS TO SELECT LOCATION, BANKSET=8
    CHECK(shows(m->panel(), "BANKSET=8"));
    m->button(Button::Bank0, true);
    runMs(*m, 60);
    m->button(Button::Bank0, false);
    CHECK(watch(*m, 2000, "LOADING " + name).text);
    const Seen done = watch(*m, 30000, "LOAD FILE=");  // back on the Load page when the sound is in
    CHECK(done.text);
    CHECK(!done.notReady && !done.notResponding);
    return m;
}

// Loads the disk's first sampled sound into S8-0, selects it and plays C-E-G for 400 ms (Section 14,
// "Selecting and Playing a Sampled Sound").
void loadsAndPlays(const char* rel) {
    std::string name;
    auto m = withFirstSound(rel, name);
    if (!m) return;
    tap(*m, Button::Sounds);
    m->button(Button::BankSet, true);  // BankSet held, Bank 8: BankSet S8
    runMs(*m, 60);
    tap(*m, Button::Bank8);
    m->button(Button::BankSet, false);
    runMs(*m, 300);
    std::printf("  |%s|\n  |%s|\n", rowText(m->panel(), 0).c_str(), rowText(m->panel(), 1).c_str());
    CHECK(shows(m->panel(), "S8-0 SAMPLED-SOUND"));
    CHECK(shows(m->panel(), name.c_str()));
    tap(*m, Button::Soft5);  // the sound's name: select it
    SampleVoices v;
    m->onBusEvent = [&v](const BusEvent& e) { v(e); };
    for (int k : {60, 64, 67}) m->key(k, true, 127);
    int peak = 0;
    const uint64_t end = m->cycles() + static_cast<uint64_t>(400) * kCpuHz / 1000;
    Audio a;
    while (m->cycles() < end) {
        m->runFrame(a);
        peak = std::max({peak, std::abs(int{a.mainL}), std::abs(int{a.mainR})});
    }
    m->onBusEvent = nullptr;
    std::printf("  peak %d, voices from sample RAM %d, emulated %.1f s\n", peak, v.count,
                static_cast<double>(m->cycles()) / kCpuHz);
    CHECK(v.count >= 3);
    CHECK(peak > 1000);
}

}  // namespace

// AD-001's first instrument (GRND PIANO 1, 3055 blocks, ASR-10 HD) into S8-0; it plays from sample RAM.
TEST(ts_loads_asr10_instrument) { loadsAndPlays(kAd001); }

// ED-001's first instrument (FLUTE 1, EPS-16 PLUS DD, GKH) the same way.
TEST(ts_loads_eps_instrument) { loadsAndPlays(kEd001); }

// DECISION A124: memory that remembers a loaded sampled sound makes the OS ask AUTO-LOAD ALL SAMPLED-SOUNDS
// at power-on (Musician's Manual, "About the Auto-Load Feature"). An engine told to answer it (the VST3's)
// presses the soft button under *NO * once, during the boot; one not told (the Standalone's) leaves the
// question up.
TEST(engine_answers_power_on_auto_load) {
    std::string name;
    auto m = withFirstSound(kEd001, name);
    if (!m) return;
    const auto ram = m->ramImage();
    m.reset();
    ens::ts::RomSet roms;
    CHECK(readTsRoms(roms));
    for (const bool answer : {false, true}) {
        std::string err;
        auto e = ens::app::Engine::create(roms, err);
        CHECK(e != nullptr);
        if (!e) return;
        e->setAnswerAutoLoad(answer);
        CHECK(e->loadRam(ram, err));
        e->prepare(48000, 480);
        float l[480], r[480], al[480], ar[480];
        bool asked = false;
        for (int b = 0; b < 600; ++b) {  // 6 s
            e->render(l, r, al, ar, 480);
            asked = asked || shows(e->machine().panel(), "AUTO-LOAD ALL SAMPLED-SOUNDS");
        }
        const bool still = shows(e->machine().panel(), "AUTO-LOAD ALL SAMPLED-SOUNDS");
        std::printf("  answer %d: asked %d, still asking %d, |%s|\n", answer, asked, still,
                    rowText(e->machine().panel(), 0).c_str());
        CHECK(asked);
        CHECK(still == !answer);
        CHECK(e->snapshot().booted);
    }
}

// A blank HD disk formatted by the OS (Section 13, "FORMAT -- Formatting a Blank Disk"), then the current
// program saved to it (Disk Save page, TYPE=1-PROGRAM). The disk written out as a raw image and read back
// into a fresh TS lists the file on its Disk Load page.
TEST(ts_format_and_save_roundtrip) {
    auto m = bootWith(Disk::blank(Density::High));
    if (!m) return;
    tap(*m, Button::Storage);
    tap(*m, Button::Soft5);  // DISK
    tap(*m, Button::Soft1);  // FORMAT: DISK NAME=..., *FORMAT*
    tap(*m, Button::Soft3);  // *FORMAT*
    CHECK(shows(m->panel(), "ALL DISK FILES WILL BE ERASED"));
    tap(*m, Button::Soft3);  // *YES*
    const Seen formatted = watch(*m, 60000, "DISK COMMAND COMPLETED");
    CHECK(formatted.text);
    CHECK(!formatted.notReady && !formatted.notResponding);
    runMs(*m, 1500);         // back to the Disk menu
    tap(*m, Button::Soft4);  // SAVE
    CHECK(shows(m->panel(), "TYPE=1-PROGRAM"));
    tap(*m, Button::Soft3);  // *YES*: SAVE FILE NAME=<the program's name>
    const std::string row = rowText(m->panel(), 0);
    const size_t at = row.find("NAME=");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    std::string name = row.substr(at + 5, 11);
    name = name.substr(name.find_first_not_of(' '));
    name = name.substr(0, name.find_last_not_of(' ') + 1);
    const uint32_t before = m->disk()->writes();
    tap(*m, Button::Soft3);  // *YES*: SAVING <name>
    CHECK(watch(*m, 10000, "DISK COMMAND COMPLETED").text);
    std::printf("  saved |%s|, writes %u then %u, emulated %.1f s\n", name.c_str(), before, m->disk()->writes(),
                static_cast<double>(m->cycles()) / kCpuHz);
    CHECK(before > 0);
    CHECK(m->disk()->writes() > before);

    DecodedImage img;
    std::string err;
    CHECK(decodeImage(encodeImage(*m->disk(), ImageFormat::Raw, {}), img, err));
    auto fresh = bootWith(std::move(img.disk));
    if (!fresh) return;
    openDiskLoad(*fresh);
    const Seen s = watch(*fresh, 3000, "TYPE=1-PROGRAM");  // the second row, written after the name
    std::printf("  |%s|\n  |%s|\n", rowText(fresh->panel(), 0).c_str(), rowText(fresh->panel(), 1).c_str());
    CHECK(s.text);
    CHECK(rowText(fresh->panel(), 0).find("LOAD FILE=") == 0);
    CHECK(shows(fresh->panel(), name.c_str()));
}
