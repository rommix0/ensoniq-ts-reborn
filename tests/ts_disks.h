#pragma once
// Helpers for the firmware tests that load from the user's floppy and CD images under ENS_DISK_DIR: ED-001 from a
// floppy (test_ts_floppy.cpp), CDR-13 at SCSI ID 4 (test_ts_scsi.cpp), the restores (test_app_media.cpp).
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cd_file.h"
#include "check.h"
#include "files.h"
#include "ts/disk_image.h"
#include "ts_firmware.h"

namespace ts_disks {

using namespace ens::ts;

inline const char* const kEd001 =
    "/EPS & EPS-16/Factory Sounds/Ensoniq EPS16 Factory Floppy Archive (15 disks, fixed) [IMG & GKH]/"
    "Ensoniq EPS16 Factory Floppy Archive (15 disks) [IMG & GKH]/GKH/ED-001.GKH";
inline const char* const kCdr13 = "/CD-ROMs/Ensoniq.CDR-13.InVision.Percussion.ENSONiQ-DELiRiUM/dlm-cdr13.cue";

// The image under ENS_DISK_DIR; nullptr with a skip line when it is missing.
inline std::unique_ptr<Disk> loadDisk(const char* rel) {
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
inline std::string firstSoundName(Disk& d) {
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
inline std::unique_ptr<Machine> bootWith(std::unique_ptr<Disk> d) {
    if (!d) return nullptr;
    auto m = loadTs();
    if (!m) return nullptr;
    CHECK(m->insertDisk(std::move(d)) == nullptr);
    runMs(*m, kBootMs);
    return m;
}

// Storage twice: the Disk Load page, which reads the disk's directory (Musician's Manual, Section 13).
inline void openDiskLoad(Machine& m) {
    tap(m, Button::Storage);
    m.button(Button::Storage, true);
    runMs(m, 60);
    m.button(Button::Storage, false);
}

struct Seen {
    bool text = false, notReady = false, notResponding = false;
};

// Runs ms, or until `text` shows (when stopAtText); what the display showed on any frame.
inline Seen watch(Machine& m, uint32_t ms, const std::string& text, bool stopAtText = true) {
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

// A machine booted with the disk that has loaded the disk's first sampled sound into S8-0 (Musician's Manual,
// Section 14, "Loading a Sampled Sound"); null without the ROMs or the image.
inline std::unique_ptr<Machine> withFirstSound(const char* rel, std::string& name) {
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

// A CD image answering the machine's sector port: called after every runFrame, it answers the pending ask from
// the file at once, so runs are deterministic. The OS polls DBR in long counted loops (c1563e), so a sector
// answered between frames is never a timeout. onRead, when set, sees each sector served.
struct ServedCd {
    CdFile file;
    uint32_t medium = 1;
    uint8_t buf[SectorPort::kBytes] = {};
    std::function<void(uint32_t lba, const uint8_t* sector)> onRead;
    void serve(Machine& m) {
        SectorPort* p = m.cdPort();
        uint32_t med = 0, lba = 0;
        if (!p || !p->pending(med, lba)) return;
        const bool ok = med == medium && file.read(lba, buf);
        if (ok && onRead) onRead(lba, buf);
        p->reply(med, lba, buf, ok);
    }
};

inline void runServed(Machine& m, ServedCd* cd, uint32_t ms) {
    const uint64_t end = m.cycles() + static_cast<uint64_t>(ms) * kCpuHz / 1000;
    Audio a;
    while (m.cycles() < end) {
        m.runFrame(a);
        if (cd) cd->serve(m);
    }
}

// tap() from ts_firmware.h with the CD served.
inline void press(Machine& m, ServedCd* cd, Button b) {
    m.button(b, true);
    runServed(m, cd, 60);
    m.button(b, false);
    runServed(m, cd, 300);
}

// Runs until `text` shows on the display or ms pass; whether it showed.
inline bool until(Machine& m, ServedCd* cd, const std::string& text, uint32_t ms) {
    const uint64_t end = m.cycles() + static_cast<uint64_t>(ms) * kCpuHz / 1000;
    Audio a;
    while (m.cycles() < end) {
        if (shows(m.panel(), text.c_str())) return true;
        m.runFrame(a);
        if (cd) cd->serve(m);
    }
    return shows(m.panel(), text.c_str());
}

inline void printRows(const Machine& m) {
    std::printf("  |%s|\n  |%s|\n", rowText(m.panel(), 0).c_str(), rowText(m.panel(), 1).c_str());
}

// The CD image under ENS_DISK_DIR opened into cd; false with a skip line when it is missing.
inline bool openCd(const char* rel, ServedCd& cd) {
    const std::string dir = ENS_DISK_DIR;
    std::string err;
    if (dir.empty() || !cd.file.open(dir + rel, err)) {
        std::printf("  skip: disk images not found\n");
        return false;
    }
    return true;
}

// A TS with SCSI, the CD (when given) in the drive at ID 4 from power-on, booted for kBootMs.
inline std::unique_ptr<Machine> bootScsi(ServedCd* cd) {
    auto m = loadTs(false, true);
    if (!m) return nullptr;
    if (cd) m->insertCd(cd->medium, cd->file.sectors());
    runServed(*m, cd, kBootMs);
    return m;
}

// The SCSI STATUS page (Musician's Manual, Section 13, "SCSI Option"), found by running: Storage shows SELECT
// STORAGE OPTION with SCSI under Soft4 (the lower left soft button); Soft4 opens "SCSI DEVICE-ID=0 STATUS=INVALID"
// with DEVICE-ID underlined (the cursor field) on a fresh memory; Up four times sets DEVICE-ID=4. The OS polls the
// ID about every 500 ms (Reset, then TEST UNIT READY, ...), so 1.5 s more lets it mount: with a disc just inserted
// the first poll gets UNIT ATTENTION and shows STANDBY, the next one INQUIRY, MODE SELECT (512-byte blocks) and
// READ(10) of block 1, and shows the volume name.
inline void openScsiStatus(Machine& m, ServedCd* cd) {
    press(m, cd, Button::Storage);
    press(m, cd, Button::Soft4);
    for (int i = 0; i < 4; ++i) press(m, cd, Button::Up);
    runServed(m, cd, 1500);
}

}  // namespace ts_disks
