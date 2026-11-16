// The media record and the restore through the OS's own auto-load (Plan 7 Task 7, A130, A131): blocks the OS read
// are kept per medium, and a fresh engine given stand-ins built from them answers the power-on AUTO-LOAD question
// *YES* and the OS's insert prompts, so the sampled sounds come back with no image file. The firmware tests need the
// ROMs and ENS_DISK_DIR.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/media_record.h"
#include "check.h"
#include "ts_disks.h"
#include "ts_firmware.h"

using namespace ens;
using namespace ts_disks;
using ens::app::Engine;
using ens::app::Event;
using ens::app::MediaRecord;
using ens::app::SavedMedium;

namespace {

const char* const kCdr15 = "/CD-ROMs/Ensoniq.CDR-15.Syntaur.L.A. RIOT.Volume.II.ENSONIQ-BSOUND/CDR-15_L.A.Riot1.iso";

// An Ensoniq Device ID block as on ED-001 and the CDs: 0xFF at byte 30, the 7-byte label at 31-37, "ID" at 38-39.
void writeId(uint8_t* block, const char* label7) {
    std::memset(block, 0, 512);
    block[30] = 0xFF;
    std::memcpy(block + 31, label7, 7);
    block[38] = 'I';
    block[39] = 'D';
}

// ---- engine side ----

constexpr int kStep = 48;  // 1 ms blocks at 48 kHz: a CD sector asked is answered about 30 frames later

// The CDs the engine's port asks for, by medium id: from saved sectors only (one the record lacks is answered as a
// failure, so the target ends the read MEDIUM ERROR and the gap shows) or from the image file (the reference).
struct Served {
    std::map<uint32_t, const SavedMedium*> saved;
    std::map<uint32_t, CdFile*> files;
    uint32_t missing = 0, asks = 0;
    uint8_t buf[ts::SectorPort::kBytes] = {};
    void serve(Engine& e) {
        ts::SectorPort* p = e.cdPort();
        uint32_t med = 0, lba = 0;
        if (!p || !p->pending(med, lba)) return;
        ++asks;
        bool ok = false;
        if (const auto s = saved.find(med); s != saved.end()) {
            const auto b = s->second->blocks.find(lba);
            ok = b != s->second->blocks.end();
            if (ok) std::memcpy(buf, b->second.data(), sizeof buf);
            else ++missing;
        } else if (const auto f = files.find(med); f != files.end()) {
            ok = f->second->read(lba, buf);
        }
        p->reply(med, lba, buf, ok);
    }
};

void run(Engine& e, Served& cds, int ms, std::vector<float>* out = nullptr, int* peak = nullptr) {
    float ml[kStep], mr[kStep], al[kStep], ar[kStep];
    for (int b = 0; b < ms; ++b) {
        e.render(ml, mr, al, ar, kStep);
        cds.serve(e);
        if (out) {
            out->insert(out->end(), ml, ml + kStep);
            out->insert(out->end(), mr, mr + kStep);
        }
        if (peak)
            for (int i = 0; i < kStep; ++i) *peak = std::max({*peak, int(std::fabs(ml[i]) * 32768), int(std::fabs(mr[i]) * 32768)});
    }
}

void post(Engine& e, Served& cds, ts::Button b) {  // 100 ms held, then 300 ms
    e.post({Event::Kind::Button, static_cast<uint8_t>(b), true, 0});
    run(e, cds, 100);
    e.post({Event::Kind::Button, static_cast<uint8_t>(b), false, 0});
    run(e, cds, 300);
}

struct Restored {
    std::unique_ptr<Engine> e;
    bool wasRestoring = false;
    std::vector<std::string> prompts;  // each distinct insert prompt seen, both rows
    std::vector<std::string> screens;  // each display held 200 ms, with its time
    double endMs = 0;
    int idleMs = 0, idleAt = 0;  // the longest time without a block read, a CD ask or a display change, from 5300 ms
};

// A fresh SCSI engine on `ram`, set up as the VST3's (A124), restoring from `s`; run until restoring ends and the
// boot is over (at most maxMs).
Restored restore(const std::vector<uint8_t>& ram, Engine::StandIns s, Served& cds, int maxMs = 90000) {
    Restored r;
    ts::RomSet roms;
    if (!readTsRoms(roms)) {
        std::printf("  skip: ROMs not found\n");
        return r;
    }
    std::string err;
    r.e = Engine::create(roms, err, true);
    CHECK(r.e != nullptr);
    if (!r.e) return r;
    r.e->setAnswerAutoLoad(true);
    CHECK(r.e->loadRam(ram, err));
    r.e->setRestore(std::move(s));
    r.e->prepare(48000, kStep);
    std::string last, logged;
    uint32_t reads = 0, asks = 0;
    int changedAt = 0, activeAt = 0;
    for (int ms = 0; ms < maxMs; ms += 10) {
        run(*r.e, cds, 10);
        const ens::app::Snapshot snap = r.e->snapshot();
        r.wasRestoring = r.wasRestoring || snap.restoring;
        const std::string rows = rowText(snap.panel, 0) + "|" + rowText(snap.panel, 1);
        if (rows != last || snap.diskReads != reads || cds.asks != asks) activeAt = ms;
        if (rows != last) changedAt = ms;
        last = rows;
        reads = snap.diskReads;
        asks = cds.asks;
        if (ms - changedAt >= 200 && rows != logged) {
            logged = rows;
            r.screens.push_back(std::to_string(changedAt) + " ms |" + rows + "|");
        }
        if (ms >= 5300 && snap.restoring && ms - activeAt > r.idleMs) {
            r.idleMs = ms - activeAt;
            r.idleAt = ms;
        }
        if (rows.rfind("INSERT DISK", 0) == 0 && rows.find("*SKIP*") != std::string::npos &&
            std::find(r.prompts.begin(), r.prompts.end(), rows) == r.prompts.end())
            r.prompts.push_back(rows);
        if (!snap.restoring && snap.booted) {
            r.endMs = ms + 10;
            break;
        }
    }
    for (const auto& p : r.screens) std::printf("  %s\n", p.c_str());
    for (const auto& p : r.prompts) std::printf("  prompt |%s|\n", p.c_str());
    std::printf("  longest idle while restoring after 5300 ms: %d ms (at %d ms)\n", r.idleMs, r.idleAt);
    std::printf("  restore ended at %.0f ms, |%s|\n", r.endMs, rowText(r.e->machine().panel(), 0).c_str());
    return r;
}

// Sounds, BankSet S8 (BankSet held, Bank 8), then `bank` for S8-1 to S8-9.
void openS8(Engine& e, Served& cds, ts::Button bank) {
    post(e, cds, ts::Button::Sounds);
    e.post({Event::Kind::Button, static_cast<uint8_t>(ts::Button::BankSet), true, 0});
    run(e, cds, 100);
    post(e, cds, ts::Button::Bank8);
    e.post({Event::Kind::Button, static_cast<uint8_t>(ts::Button::BankSet), false, 0});
    run(e, cds, 300);
    if (bank != ts::Button::Bank0) post(e, cds, bank);
    std::printf("  |%s|\n  |%s|\n", rowText(e.machine().panel(), 0).c_str(), rowText(e.machine().panel(), 1).c_str());
}

// openS8, the sound selected (Soft5, its name on that page), then C-E-G for 400 ms: Main L and R, the voices started
// from sample RAM and the peak.
std::vector<float> playS8(Engine& e, Served& cds, ts::Button bank, int& voices, int& peak, const std::string& name) {
    openS8(e, cds, bank);
    CHECK(shows(e.machine().panel(), name.c_str()));
    post(e, cds, ts::Button::Soft5);
    SampleVoices v;
    e.machine().onBusEvent = [&v](const ts::BusEvent& ev) { v(ev); };
    for (int k : {60, 64, 67}) e.post({Event::Kind::Key, static_cast<uint8_t>(k), true, 127});
    std::vector<float> out;
    peak = 0;
    run(e, cds, 400, &out, &peak);
    e.machine().onBusEvent = nullptr;
    for (int k : {60, 64, 67}) e.post({Event::Kind::Key, static_cast<uint8_t>(k), false, 0});
    run(e, cds, 300);
    voices = v.count;
    std::printf("  %s: voices from sample RAM %d, peak %d\n", name.c_str(), voices, peak);
    return out;
}

}  // namespace

// The label the OS shows for a disk or volume is the 7 bytes at 31-37 of the Device ID block (block 1), trimmed:
// ED-001's gives ED-001 (the OS prompts "NAME  ED-001", the label's leading space included).
TEST(media_label_from_id_block) {
    uint8_t id[512];
    writeId(id, "CDR-013");
    CHECK_EQ(app::ensoniqLabel(id), std::string("CDR-013"));
    writeId(id, " ED-1  ");
    CHECK_EQ(app::ensoniqLabel(id), std::string("ED-1"));
    id[38] = 0;  // no "ID": not an Ensoniq ID block
    CHECK_EQ(app::ensoniqLabel(id), std::string());
    auto d = loadDisk(kEd001);
    if (!d) return;
    CHECK_EQ(app::ensoniqLabel(d->block(1)), std::string("ED-001"));
}

// Blocks are kept from their first capture: a later copy with a block changed keeps the old bytes and adds new reads.
// The stand-in holds the saved blocks and zeros elsewhere, formatted, with no reads marked.
TEST(media_capture_first_wins) {
    auto d = ts::Disk::formatted(ts::Density::Double);
    writeId(d->block(1), "TEST   ");
    d->block(3)[0] = 1;
    d->block(4)[0] = 2;
    d->noteRead(3);
    d->noteRead(4);
    MediaRecord rec;
    rec.captureFloppy(*d);
    CHECK_EQ(rec.media().size(), 1u);
    if (rec.media().size() != 1) return;
    CHECK(rec.media()[0].kind == SavedMedium::Kind::FloppyDD);
    CHECK_EQ(rec.media()[0].label, std::string("TEST"));
    CHECK_EQ(rec.media()[0].size, 1600u);
    CHECK_EQ(rec.media()[0].blocks.size(), 2u);
    d->block(3)[0] = 9;
    d->block(5)[0] = 5;
    d->noteRead(5);
    rec.captureFloppy(*d);
    CHECK_EQ(rec.media().size(), 1u);
    const SavedMedium& m = rec.media()[0];
    CHECK_EQ(m.blocks.size(), 3u);
    CHECK_EQ(m.blocks.at(3).size(), 512u);
    CHECK_EQ(m.blocks.at(3)[0], 1);
    CHECK_EQ(m.blocks.at(5)[0], 5);
    auto s = app::standInFloppy(m);
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(s->density() == ts::Density::Double);
    CHECK(s->trackFormatted(79, 1));
    CHECK_EQ(s->block(3)[0], 1);
    CHECK_EQ(s->block(5)[0], 5);
    CHECK_EQ(s->block(1)[0], 0);  // block 1 was never read: not saved
    CHECK_EQ(s->reads(), 0u);
    rec.clear();
    CHECK(rec.media().empty());
    rec.captureFloppy(*ts::Disk::formatted(ts::Density::High));  // nothing read: no medium
    CHECK(rec.media().empty());
}

// CD sectors go to the medium whose sector 0 (blocks 0-3, the ID block at 1) was served last; a volume seen again is
// the same medium. Sectors before any sector 0 are dropped.
TEST(media_capture_cd_by_volume) {
    uint8_t a[2048] = {}, b[2048] = {}, x[2048] = {};
    writeId(a + 512, "VOL-A  ");
    writeId(b + 512, "VOL-B  ");
    x[0] = 7;
    MediaRecord rec;
    rec.captureCd(100, 5, x);
    CHECK(rec.media().empty());
    rec.captureCd(100, 0, a);
    rec.captureCd(100, 5, x);
    rec.captureCd(200, 0, b);
    rec.captureCd(200, 6, x);
    rec.captureCd(100, 0, a);
    rec.captureCd(100, 7, x);
    CHECK_EQ(rec.media().size(), 2u);
    if (rec.media().size() != 2) return;
    const SavedMedium &ma = rec.media()[0], &mb = rec.media()[1];
    CHECK(ma.kind == SavedMedium::Kind::Cd);
    CHECK_EQ(ma.label, std::string("VOL-A"));
    CHECK_EQ(ma.size, 100u);
    CHECK_EQ(ma.blocks.size(), 3u);  // 0, 5, 7
    CHECK_EQ(ma.blocks.at(5).size(), 2048u);
    CHECK_EQ(mb.label, std::string("VOL-B"));
    CHECK_EQ(mb.blocks.size(), 2u);  // 0, 6
    CHECK(app::standInFloppy(ma) == nullptr);
}

// The spike as a test: ED-001's FLUTE 1 loaded into S8-0 and the blocks the OS read recorded; a fresh engine on that
// RAM with only the recorded floppy as a stand-in answers *YES*, the OS loads FLUTE 1 from it without a prompt, the
// restore ends with the drive empty, and C-E-G renders the same samples as a restore from the full ED-001 disk.
TEST(engine_restore_floppy) {
    std::string name;
    auto m = withFirstSound(kEd001, name);
    if (!m) return;
    MediaRecord rec;
    rec.captureFloppy(*m->disk());
    CHECK_EQ(rec.media().size(), 1u);
    if (rec.media().size() != 1) return;
    const SavedMedium& f = rec.media()[0];
    CHECK_EQ(f.label, std::string("ED-001"));
    std::printf("  %s: %zu blocks recorded\n", name.c_str(), f.blocks.size());
    const auto ram = m->ramImage();
    m.reset();
    std::vector<float> want, got;
    for (const bool standIn : {false, true}) {
        Engine::StandIns s;
        s.floppies.push_back(standIn ? app::standInFloppy(f) : loadDisk(kEd001));
        s.floppyLabels.push_back(f.label);
        Served cds;
        Restored r = restore(ram, std::move(s), cds);
        if (!r.e) return;
        CHECK(r.wasRestoring);
        CHECK(r.prompts.empty());
        CHECK(!r.e->snapshot().restoring);
        CHECK(!r.e->snapshot().diskIn);
        auto back = r.e->takeStandIns();
        CHECK_EQ(back.size(), 1u);
        if (back.size() != 1 || !back[0]) return;
        if (standIn) {  // the auto-load read only recorded blocks
            int outside = 0;
            for (int b = 0; b < 1600; ++b) outside += back[0]->wasRead(b) && !f.blocks.count(uint32_t(b));
            std::printf("  auto-load read %u blocks, %d not recorded\n", back[0]->reads(), outside);
            CHECK(back[0]->reads() > 0);
            CHECK_EQ(outside, 0);
        }
        int voices = 0, peak = 0;
        (standIn ? got : want) = playS8(*r.e, cds, ts::Button::Bank0, voices, peak, name);
        CHECK(voices >= 3);
        CHECK(peak > 1000);
    }
    CHECK(!want.empty());
    CHECK(got == want);
}

// The same for a CD: CDR-13's BONGOS 1 (546 blocks, PERCUSSION/DRUMS) loaded into S8-0 with the sectors served
// recorded; the stand-in CD is served from the recorded sectors only.
TEST(engine_restore_cd) {
    ServedCd cd;
    if (!openCd(kCdr13, cd)) return;
    MediaRecord rec;
    cd.onRead = [&](uint32_t lba, const uint8_t* s) { rec.captureCd(cd.file.sectors(), lba, s); };
    auto m = bootScsi(&cd);
    if (!m) return;
    openScsiStatus(*m, &cd);
    CHECK(loadFromCd(*m, cd, {"PERCUSSION", "DRUMS", "BONGOS 1"}, ts::Button::Bank0));
    CHECK_EQ(rec.media().size(), 1u);
    if (rec.media().size() != 1) return;
    const SavedMedium& c = rec.media()[0];
    CHECK_EQ(c.label, std::string("CDR-013"));
    std::printf("  %zu sectors recorded of %u\n", c.blocks.size(), c.size);
    const auto ram = m->ramImage();
    m.reset();
    std::vector<float> want, got;
    for (const bool standIn : {false, true}) {
        Engine::StandIns s;
        s.cdMedia.push_back(standIn ? 12 : 11);
        s.cdSizes.push_back(c.size);
        s.cdLabels.push_back(c.label);
        Served cds;
        if (standIn) cds.saved[12] = &c;
        else cds.files[11] = &cd.file;
        Restored r = restore(ram, std::move(s), cds);
        if (!r.e) return;
        CHECK(r.wasRestoring);
        CHECK(r.prompts.empty());
        CHECK_EQ(cds.missing, 0u);
        CHECK_EQ(r.e->snapshot().cdMedium, 0u);  // ejected at the end
        CHECK(r.e->takeStandIns().empty());
        int voices = 0, peak = 0;
        (standIn ? got : want) = playS8(*r.e, cds, ts::Button::Bank0, voices, peak, "BONGOS 1");
        CHECK(voices >= 3);
        CHECK(peak > 1000);
    }
    CHECK(!want.empty());
    CHECK(got == want);
}

// Two volumes at ID 4: CDR-13's BONGOS 1 (546 blocks) into S8-0 and CDR-15's LIS-10 SCR 4 (1004 blocks, L A
// RIOT-1/LIS-10) into S8-1, small enough to fit together in the 4018 free blocks. At restore the OS loads S8-0 from
// the first stand-in, then asks "INSERT DISK WITH NAME CDR-015 / INTO SCSI DEVICE 4": the engine swaps in that
// volume's stand-in and presses *OK*, and both sounds play from sample RAM.
TEST(engine_restore_two_cds) {
    ServedCd cd13, cd15;
    if (!openCd(kCdr13, cd13) || !openCd(kCdr15, cd15)) return;
    cd15.medium = 2;
    MediaRecord rec;
    cd13.onRead = [&](uint32_t lba, const uint8_t* s) { rec.captureCd(cd13.file.sectors(), lba, s); };
    cd15.onRead = [&](uint32_t lba, const uint8_t* s) { rec.captureCd(cd15.file.sectors(), lba, s); };
    auto m = bootScsi(&cd13);
    if (!m) return;
    openScsiStatus(*m, &cd13);
    CHECK(loadFromCd(*m, cd13, {"PERCUSSION", "DRUMS", "BONGOS 1"}, ts::Button::Bank0));
    m->insertCd(cd15.medium, cd15.file.sectors());
    press(*m, &cd15, ts::Button::Storage);
    press(*m, &cd15, ts::Button::Soft4);  // the SCSI page, DEVICE-ID=4 kept
    runServed(*m, &cd15, 1500);
    printRows(*m);
    CHECK(shows(m->panel(), "CDR-015"));
    CHECK(loadFromCd(*m, cd15, {"L A RIOT-1", "LIS-10", "LIS-10 SCR 4"}, ts::Button::Bank1));
    CHECK_EQ(rec.media().size(), 2u);
    if (rec.media().size() != 2) return;
    CHECK_EQ(rec.media()[0].label, std::string("CDR-013"));
    CHECK_EQ(rec.media()[1].label, std::string("CDR-015"));
    const auto ram = m->ramImage();
    m.reset();
    Engine::StandIns s;
    Served cds;
    for (size_t i = 0; i < 2; ++i) {
        s.cdMedia.push_back(uint32_t(21 + i));
        s.cdSizes.push_back(rec.media()[i].size);
        s.cdLabels.push_back(rec.media()[i].label);
        cds.saved[uint32_t(21 + i)] = &rec.media()[i];
    }
    Restored r = restore(ram, std::move(s), cds);
    if (!r.e) return;
    CHECK(r.wasRestoring);
    CHECK_EQ(r.prompts.size(), 1u);
    CHECK(!r.prompts.empty() && r.prompts[0].find("CDR-015") != std::string::npos);
    CHECK_EQ(cds.missing, 0u);
    int voices = 0, peak = 0;
    playS8(*r.e, cds, ts::Button::Bank0, voices, peak, "BONGOS 1");
    CHECK(voices >= 3);
    CHECK(peak > 1000);
    playS8(*r.e, cds, ts::Button::Bank1, voices, peak, "LIS-10 SCR 4");
    CHECK(voices >= 3);
    CHECK(peak > 1000);
}

// Review Focus 4: the OS asks for a disk the stand-ins do not hold (memory remembering ED-001's FLUTE 1, one floppy
// stand-in named OTHER): the engine presses *SKIP*, the boot ends and restoring is over.
TEST(engine_restore_skips_unknown_disk) {
    std::string name;
    auto m = withFirstSound(kEd001, name);
    if (!m) return;
    MediaRecord rec;
    rec.captureFloppy(*m->disk());
    if (rec.media().size() != 1) return;
    SavedMedium other = rec.media()[0];
    CHECK(other.blocks.count(1) == 1);
    std::memcpy(other.blocks[1].data() + 31, "  OTHER", 7);  // the same disk, renamed
    other.label = "OTHER";
    const auto ram = m->ramImage();
    m.reset();
    Engine::StandIns s;
    s.floppies.push_back(app::standInFloppy(other));
    s.floppyLabels.push_back(other.label);
    Served cds;
    Restored r = restore(ram, std::move(s), cds);
    if (!r.e) return;
    CHECK(r.wasRestoring);
    CHECK_EQ(r.prompts.size(), 1u);
    CHECK(!r.prompts.empty() && r.prompts[0].find("ED-001") != std::string::npos);
    CHECK(r.endMs > 0);
    CHECK(!r.e->snapshot().restoring);
    CHECK(r.e->snapshot().booted);
    CHECK(rowText(r.e->machine().panel(), 0).rfind("U0-0 ", 0) == 0);
}

// Stand-ins for memory that remembers no sampled sound (a cold memory): the OS never asks, the restore ends at the
// boot time (A104) on U0-0, the drive is left empty and the stand-in comes back unread.
TEST(engine_restore_without_question_ends_at_boot) {
    ts::RomSet roms;
    if (!readTsRoms(roms)) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    std::string err;
    auto e = Engine::create(roms, err);
    CHECK(e != nullptr);
    if (!e) return;
    Engine::StandIns s;
    s.floppies.push_back(ts::Disk::formatted(ts::Density::Double));
    s.floppyLabels.push_back("ED-001");
    e->setRestore(std::move(s));
    e->prepare(48000, 480);
    float l[480], r[480], al[480], ar[480];
    for (int b = 0; b < 100; ++b) e->render(l, r, al, ar, 480);
    CHECK(e->snapshot().restoring);
    for (int b = 100; b < 540; ++b) e->render(l, r, al, ar, 480);  // 5.4 s
    const ens::app::Snapshot snap = e->snapshot();
    std::printf("  |%s|\n", rowText(snap.panel, 0).c_str());
    CHECK(!snap.restoring);
    CHECK(snap.booted);
    CHECK(!snap.diskIn);
    auto back = e->takeStandIns();
    CHECK(back.size() == 1 && back[0] && back[0]->reads() == 0);
}

// Review: a stand-in CD missing a sector the auto-load reads (the last recorded one, sample data of BONGOS 1). The
// test answers it as a failure, so the target ends that read MEDIUM ERROR (T28). The restore still ends in seconds,
// on the play screen, with that sound skipped (it held the boot for 120 s before the error page was answered).
TEST(engine_restore_missing_sector) {
    ServedCd cd;
    if (!openCd(kCdr13, cd)) return;
    MediaRecord rec;
    cd.onRead = [&](uint32_t lba, const uint8_t* s) { rec.captureCd(cd.file.sectors(), lba, s); };
    auto m = bootScsi(&cd);
    if (!m) return;
    openScsiStatus(*m, &cd);
    CHECK(loadFromCd(*m, cd, {"PERCUSSION", "DRUMS", "BONGOS 1"}, ts::Button::Bank0));
    if (rec.media().size() != 1) return;
    SavedMedium c = rec.media()[0];
    const auto last = std::prev(c.blocks.end());
    CHECK(std::any_of(last->second.begin(), last->second.end(), [](uint8_t b) { return b != 0; }));
    std::printf("  sector %u erased\n", last->first);
    c.blocks.erase(last);
    const auto ram = m->ramImage();
    m.reset();
    Engine::StandIns s;
    s.cdMedia.push_back(31);
    s.cdSizes.push_back(c.size);
    s.cdLabels.push_back(c.label);
    Served cds;
    cds.saved[31] = &c;
    Restored r = restore(ram, std::move(s), cds, 150000);
    if (!r.e) return;
    CHECK(r.wasRestoring);
    CHECK(cds.missing >= 1);
    // What the OS did: DISK DRIVE NOT RESPONDING / PRESS ANY BUTTON TO CONTINUE... (the engine presses Up), then
    // INSERT DISK WITH NAME UNKNOWN / INTO SCSI DEVICE 4 (no stand-in has that name: *SKIP*), then U0-0.
    CHECK(std::any_of(r.screens.begin(), r.screens.end(),
                      [](const std::string& x) { return x.find("DISK DRIVE NOT RESPONDING") != std::string::npos; }));
    CHECK_EQ(r.prompts.size(), 1u);
    CHECK(!r.prompts.empty() && r.prompts[0].find("NAME UNKNOWN") != std::string::npos);
    CHECK(r.endMs > 0 && r.endMs < 30000);
    CHECK(rowText(r.e->machine().panel(), 0).rfind("U0-0 ", 0) == 0);
    CHECK_EQ(r.e->snapshot().cdMedium, 0u);
    CHECK(r.e->snapshot().booted);
    openS8(*r.e, cds, ts::Button::Bank0);
    CHECK(shows(r.e->machine().panel(), "--NOT LOADED--"));  // BONGOS 1 skipped, the name kept
}

// Review: a prompt the OS shows again after *OK*: the stand-in is listed as ED-001 but its ID block says OTHER, so
// the OS asks for ED-001 again after the engine's *OK*. The engine answers *SKIP* the second time and the boot ends.
TEST(engine_restore_skips_prompt_shown_again) {
    std::string name;
    auto m = withFirstSound(kEd001, name);
    if (!m) return;
    MediaRecord rec;
    rec.captureFloppy(*m->disk());
    if (rec.media().size() != 1) return;
    SavedMedium wrong = rec.media()[0];
    std::memcpy(wrong.blocks[1].data() + 31, "  OTHER", 7);  // the label stays ED-001 in the stand-ins
    const auto ram = m->ramImage();
    m.reset();
    Engine::StandIns s;
    s.floppies.push_back(app::standInFloppy(wrong));
    s.floppyLabels.push_back("ED-001");
    Served cds;
    Restored r = restore(ram, std::move(s), cds, 150000);
    if (!r.e) return;
    CHECK(r.wasRestoring);
    CHECK_EQ(r.prompts.size(), 1u);  // asked for ED-001 (twice: *OK*, then *SKIP*)
    CHECK(r.endMs > 0 && r.endMs < 30000);
    CHECK(!r.e->snapshot().diskIn);
    CHECK(rowText(r.e->machine().panel(), 0).rfind("U0-0 ", 0) == 0);
}
