#include "Processor.h"

#include <algorithm>
#include <cmath>

#include "MemoryFile.h"
#include "PanelEditor.h"
#include "RomFolder.h"
#include "app/analog_map.h"

namespace {

// The first file that is not good, as two display rows; empty when all six are good.
juce::String romProblem(const std::vector<RomFileStatus>& list) {
    for (const auto& s : list)
        if (s.state != RomFileStatus::Found)
            return juce::String(s.state == RomFileStatus::Missing ? "ROM missing:\n" : "ROM wrong size:\n") + s.file;
    return {};
}

// Why file cannot be the memory file, or empty. Not in the ROM folder (os/ts10_310l.bin is 512 KB
// too, and the 60 s save would overwrite it), and a file that can be written.
juce::String memoryInRomFolder(const juce::File& file, const juce::File& roms) {
    if (roms != juce::File() && file.isAChildOf(roms))
        return "The memory file cannot be in the ROM folder: " + file.getFullPathName();
    return {};
}

juce::String memoryTargetProblem(const juce::File& file, const juce::File& roms) {
    if (const auto err = memoryInRomFolder(file, roms); err.isNotEmpty()) return err;
    if (file.isDirectory() || !file.hasWriteAccess())  // a new file: its folder's access
        return "The memory file cannot be written: " + file.getFullPathName();
    return {};
}

using ens::ts::Analog;
struct AnalogParam {
    const char* id;
    const char* name;
    Analog analog;
};
// Order matters: TsProcessor::analogRaw_ follows it.
constexpr AnalogParam kAnalogParams[] = {{"volume", "Volume", Analog::Volume},
                                         {"dataEntry", "Data Entry", Analog::DataEntry},
                                         {"pitchWheel", "Pitch Wheel", Analog::PitchWheel},
                                         {"modWheel", "Mod Wheel", Analog::ModWheel},
                                         {"cvPedal", "CV Pedal", Analog::CvPedal}};
static_assert(std::size(kAnalogParams) == 5, "TsProcessor::analogRaw_ has five slots");

// DECISION A129: <Media> in a Project state: one <Medium kind="floppy-dd|floppy-hd|cd" label="..." size="N"> per
// medium the OS read from (size in blocks: 1600 or 3200 for a floppy, the sectors of a CD), with one
// <Run first="b" count="k"> per stretch of consecutive blocks, the bytes in base64 (512 a floppy block, 2048 a CD
// sector). The label's bytes are kept as Latin-1 characters, so any byte the OS shows comes back as it was.
using ens::app::SavedMedium;

const char* kindName(SavedMedium::Kind k) {
    return k == SavedMedium::Kind::FloppyDD ? "floppy-dd" : k == SavedMedium::Kind::FloppyHD ? "floppy-hd" : "cd";
}

size_t blockBytes(SavedMedium::Kind k) {
    return k == SavedMedium::Kind::Cd ? size_t(2048) : size_t(ens::ts::Disk::kSectorBytes);
}

std::unique_ptr<juce::XmlElement> mediaXml(const std::vector<SavedMedium>& media) {
    auto x = std::make_unique<juce::XmlElement>("Media");
    for (const auto& m : media) {
        auto* e = x->createNewChildElement("Medium");
        e->setAttribute("kind", kindName(m.kind));
        juce::String label;
        for (const char c : m.label) label += static_cast<juce::juce_wchar>(static_cast<uint8_t>(c));
        e->setAttribute("label", label);
        e->setAttribute("size", juce::String(m.size));
        for (auto it = m.blocks.begin(); it != m.blocks.end();) {
            const uint32_t first = it->first;
            uint32_t count = 0;
            juce::MemoryBlock run;
            for (; it != m.blocks.end() && it->first == first + count; ++it, ++count)
                run.append(it->second.data(), it->second.size());
            auto* r = e->createNewChildElement("Run");
            r->setAttribute("first", juce::String(first));
            r->setAttribute("count", juce::String(count));
            r->addTextElement(juce::Base64::toBase64(run.getData(), run.getSize()));
        }
    }
    return x;
}

// DECISION A129 (Review Focus 5): a medium that is not whole is dropped and the rest restore: an unknown kind, a floppy
// size other than 1600 (DD) or 3200 (HD) or a super disk's (T30: whole cylinders, 80 to 255), no CD sectors, a
// label character past 0xFF, a run outside the medium, bad base64 or a run whose bytes are not count blocks, or no
// blocks at all.
bool parseMedium(const juce::XmlElement& e, SavedMedium& m) {
    const auto kind = e.getStringAttribute("kind");
    const juce::int64 size = e.getStringAttribute("size").getLargeIntValue();
    const auto cylinders = [size](int perCylinder) {
        return size % perCylinder == 0 && size / perCylinder >= ens::ts::Disk::kCylinders &&
               size / perCylinder <= ens::ts::Disk::kMaxCylinders;
    };
    if (kind == "floppy-dd" && cylinders(20))
        m.kind = SavedMedium::Kind::FloppyDD;
    else if (kind == "floppy-hd" && cylinders(40))
        m.kind = SavedMedium::Kind::FloppyHD;
    else if (kind == "cd" && size > 0 && size <= 0xFFFFFFFF)
        m.kind = SavedMedium::Kind::Cd;
    else
        return false;
    m.size = static_cast<uint32_t>(size);
    for (const auto c : e.getStringAttribute("label")) {
        if (c > 0xFF) return false;
        m.label += static_cast<char>(c);
    }
    const size_t bytes = blockBytes(m.kind);
    for (const auto* r : e.getChildWithTagNameIterator("Run")) {
        const juce::int64 first = r->getStringAttribute("first").getLargeIntValue();
        const juce::int64 count = r->getStringAttribute("count").getLargeIntValue();
        if (first < 0 || first >= size || count <= 0 || count > size - first) return false;
        juce::MemoryOutputStream data;
        if (!juce::Base64::convertFromBase64(data, r->getAllSubText()) ||
            data.getDataSize() != static_cast<size_t>(count) * bytes)
            return false;
        const auto* p = static_cast<const uint8_t*>(data.getData());
        for (juce::int64 i = 0; i < count; ++i, p += bytes)
            m.blocks.emplace(static_cast<uint32_t>(first + i), std::vector<uint8_t>(p, p + bytes));
    }
    return !m.blocks.empty();
}

std::vector<SavedMedium> parseMedia(const juce::XmlElement* x) {
    std::vector<SavedMedium> media;
    if (x != nullptr)
        for (const auto* e : x->getChildWithTagNameIterator("Medium"))
            if (SavedMedium m; parseMedium(*e, m)) media.push_back(std::move(m));
    return media;
}

}  // namespace

// DECISION A111: the five analogs and the sound are host parameters (automatable): floats 0..1 whose
// defaults are the rest positions (T8, A110), and `sound` 0..127. A position maps to raw by analog_map.h.
juce::AudioProcessorValueTreeState::ParameterLayout TsProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& a : kAnalogParams)
        l.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(a.id, 1), a.name, juce::NormalisableRange<float>(0.0f, 1.0f),
            ens::app::analogPosition(a.analog, ens::ts::kAnalogRest[static_cast<size_t>(a.analog)])));
    l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID("sound", 1), "Sound", 0, 127, 0));
    return l;
}

// DECISION A109: Main and Aux, stereo. The Standalone starts with Aux off, so it plays both on one pair.
TsProcessor::TsProcessor(MemoryMode mode)
    : AudioProcessor(BusesProperties()
                         .withOutput("Main", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Aux", juce::AudioChannelSet::stereo(),
                                     juce::PluginHostType::getPluginLoadedAs() != wrapperType_Standalone)),
      mode_(mode), params_(*this, nullptr, "TS", layout()) {
    for (size_t i = 0; i < std::size(kAnalogParams); ++i)
        analogRaw_[i] = params_.getRawParameterValue(kAnalogParams[i].id);
    sound_ = params_.getRawParameterValue("sound");
    // No compiled-in default. DECISION A125: the stored folder, "roms" beside the program (the release
    // folder's layout, stored so the other format finds it), or the VST3 package's own ROMs (not stored);
    // with none, the first start opens with no ROMs.
    const auto choice =
        findRomFolder(settings_.romFolder(), juce::File::getSpecialLocation(juce::File::currentExecutableFile));
    if (choice.dir != juce::File())
        setRomFolder(choice.dir, romError_, choice.store);
    else
        romError_ = "No ROM folder:\nopen Settings";
    startTimerHz(30);  // DECISION A111: one 30 Hz timer; the 60 s save counts 1800 ticks (A116)
}

TsProcessor::~TsProcessor() {
    stopTimer();
    delete sysexIn_.exchange(nullptr);  // audio stopped: whatever a slot holds
    delete sysexDone_.exchange(nullptr);
    delete sysex_;
    // DECISION A116: saved on close only once booted; audio has stopped, so the machine is read directly.
    // DECISION A117: File mode only; a plugin instance's memory is in the project.
    if (mode_ == MemoryMode::File && engine_ && engine_->snapshot().booted)
        saveMemoryFile(settings_.memoryFile(), engine_->machine().ramImage());
}

// Refused, with the reason in err, when the memory file is inside the new folder: it would load the
// OS ROM as RAM and the 60 s save would overwrite it (setMemoryFile guards the other order).
bool TsProcessor::setRomFolder(const juce::File& dir, juce::String& err, bool store) {
    err = memoryInRomFolder(settings_.memoryFile(), dir);
    if (err.isNotEmpty()) return false;
    if (store) settings_.setRomFolder(dir);  // DECISION A125: a VST3 package's own ROMs are not stored
    romFolder_ = dir;
    romStored_ = settings_.romFolder() == dir;
    ens::ts::RomSet roms;
    const auto problem = romProblem(checkRomFolder(dir, &roms));
    if (problem.isNotEmpty()) {
        if (!engine_) romError_ = problem;
        return false;  // a running machine keeps running
    }
    roms_ = std::move(roms);
    const std::lock_guard<std::mutex> lock(stateMu_);
    return restart(Ram::Keep);
}

bool TsProcessor::setMemoryFile(const juce::File& file, juce::String& err) {
    err = memoryTargetProblem(file, romFolder_);  // DECISION A125: the folder in use, stored or not
    if (err.isNotEmpty()) return false;
    if (file.existsAsFile()) {
        loadMemoryFile(file, err);
        if (err.isNotEmpty()) return false;
    }
    settings_.setMemoryFile(file);
    memoryError_ = {};
    if (file.existsAsFile() && engine_) {
        const std::lock_guard<std::mutex> lock(stateMu_);
        restart(Ram::File);
    }
    return true;
}

// DECISION A116: erase writes the 512 KB zero image to the memory file at once (an explicit act, not
// held back by the boot gate) and restarts the machine from it, so the firmware rebuilds its memory as
// it boots. With no machine, only the file is erased. If the write fails, nothing is erased and the
// machine runs on; the settings page says so.
// DECISION A117: in Project mode this instance alone restarts cold from zeros; the shared file is left
// alone. Either way the machine is cold on U0-0: no select to replay and `sound` 0, set after the restart
// (outside the state mutex: a host may save from its notification), long before the boot ends.
void TsProcessor::eraseMemory() {
    {
        const std::lock_guard<std::mutex> lock(stateMu_);
        if (mode_ == MemoryMode::File) {
            if (!saved(eraseMemoryFile(settings_.memoryFile()))) return;
            memoryError_ = {};
        }
        startSelect_.clear();
        restoreWanted_ = false;  // DECISION A129: a cold machine restores no project's sounds
        if (mode_ == MemoryMode::Project || engine_) {  // File mode with no machine: the file alone
            startImage_.assign(static_cast<size_t>(kMemoryBytes), 0);
            haveStart_ = true;
            if (engine_) restart(Ram::Start);
        }
    }
    if (auto* p = params_.getParameter("sound"); p != nullptr && soundNow() != 0) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));
        p->endChangeGesture();
    }
}

// A failed save is shown on the settings page (memoryError) until a save succeeds.
bool TsProcessor::saved(bool ok) {
    const auto msg = "Memory not saved: " + settings_.memoryFile().getFullPathName();
    if (!ok)
        memoryError_ = msg;
    else if (memoryError_ == msg)
        memoryError_ = {};
    return ok;
}

bool TsProcessor::restart(Ram ram, bool fromState) {
    suspendProcessing(true);  // from here the audio thread is out of processBlock, and stays out
    const auto snap = engine_ ? engine_->snapshot() : ens::app::Snapshot{};
    // Before tick saw it; not for a state load, which has already replaced restored_ and cleared the record with the
    // new project's (A129): finishing would merge the new media into the old restore's and skip the new restore.
    if (!fromState && restoreRan_ && snap.frames > 0 && !snap.restoring) finishRestore(snap.restoreClean);
    std::vector<uint8_t> img;
    // DECISION A117: Start replays the start image's select; Keep and File keep the select a state would
    // save (the running one once booted, else the start one), so a new memory file keeps the sound.
    auto sel = ram == Ram::Start ? startSelect_ : select(snap);
    bool fromStart = false;
    if (ram == Ram::Keep && snap.booted) {
        img = engine_->machine().ramImage();  // audio stopped: the machine is ours to read
    } else if (ram != Ram::File && haveStart_) {
        img = startImage_;  // DECISION A116, A117: never a half-booted image, nor the file over a project's
        fromStart = true;
    } else {
        img = loadMemoryFile(settings_.memoryFile(), memoryError_);  // wrong size: reported, cold boot
    }
    std::string e;
    // DECISION A128: every machine has the SP-4 SCSI board with the CD drive at ID 4. DECISION A135: and the 4M x 8
    // SIMM pair, 8 MB of sample RAM (BankSets S8 and S9).
    auto fresh = ens::app::Engine::create(roms_, e, true, true);
    if (fresh) fresh->setMidiOut(&TsProcessor::addMidiOut, this);
    // DECISION A124: the VST3 answers the power-on auto-load question; the Standalone asks, as the TS does.
    if (fresh) fresh->setAnswerAutoLoad(mode_ == MemoryMode::Project);
    if (fresh && !img.empty() && !fresh->loadRam(img, e)) fresh.reset();
    const bool ok = fresh != nullptr;
    if (ok) {
        fresh->setSelect(sel.data(), static_cast<int>(sel.size()));
        if (rate_ > 0) fresh->prepare(rate_, block_);
        std::unique_ptr<ens::ts::Disk> disk;
        std::pair<uint32_t, uint32_t> cd{0, 0};
        if (engine_) {
            disk = engine_->releaseDisk();
            cd = engine_->releaseCd();
        }
        // DECISION A129: a stand-in of a restore that has not ended is not the user's: it goes with its engine.
        if (std::find(standInDisks_.begin(), standInDisks_.end(), disk.get()) != standInDisks_.end()) disk.reset();
        if (std::find(standInCds_.begin(), standInCds_.end(), cd.first) != standInCds_.end()) cd = {0, 0};
        unserve();
        standInCds_.clear();
        standInDisks_.clear();
        restoreRan_ = false;
        // DECISION A129: a project's media go with the RAM they came with; any other start drops them.
        if (!fromStart || !restoreWanted_) {
            restored_.clear();
            restoreWanted_ = false;
        } else {
            // Floppies through standInFloppy, CDs served by the CD drive from restored_ under fresh ids (A131).
            ens::app::Engine::StandIns s;
            for (const auto& m : restored_) {
                if (m.kind == SavedMedium::Kind::Cd) {
                    const uint32_t id = cd_.newMedium();
                    cd_.serveFrom(&m, id);
                    standInCds_.push_back(id);
                    s.cdMedia.push_back(id);
                    s.cdSizes.push_back(m.size);
                    s.cdLabels.push_back(m.label);
                } else {
                    s.floppies.push_back(ens::app::standInFloppy(m));
                    standInDisks_.push_back(s.floppies.back().get());
                    s.floppyLabels.push_back(m.label);
                }
            }
            fresh->setRestore(std::move(s));  // after loadRam, the drive empty, before the audio resumes
            restoreRan_ = true;
        }
        // DECISION A129: the record starts again at a project's restore, so the user's disk counts only new reads.
        if (disk && fromState) disk->clearReads();
        // DECISION A119: the disk in the drive goes into the new machine (the OS sees a disk change).
        // DECISION A129: with a restore it waits in the slot and goes in once the restore ends (A131).
        if (disk) fresh->insertDisk(std::move(disk));
        // DECISION A128: so does the CD, under the same medium id (the same image); A129 as the disk.
        if (cd.second != 0) fresh->insertCd(cd.first, cd.second);
        engine_.swap(fresh);  // fresh now holds the old engine, freed below after the resume
        cd_.setEngine(engine_.get());  // before the old engine (and its port) is freed
        startImage_ = std::move(img);
        startSelect_ = std::move(sel);
        haveStart_ = true;
        ramPending_ = false;
        lastSeq_ = 0;    // a new engine counts from 0
        followed_ = -1;  // a stale follow marker would make the new engine adopt the sound, not send it
        ticks_ = 0;
        romError_ = {};
        ++engineGen_;
    } else if (!engine_) {
        romError_ = juce::String(e);
    }
    suspendProcessing(false);
    return ok;
}

// DECISION A129: the floppy stand-ins come back with the blocks the auto-load read marked, and the record (cleared at
// the restore) keeps them; the CD's sectors reached it as they were served. A restore that did not end clean (a
// *SKIP*, an error page, the idle or cap end) left a sound unloaded that the RAM still names: the restored media stay
// in the record under what was read, so the next save does not lose them. restored_ is dropped once the drive no
// longer serves from it. The caller holds stateMu_; the restore of the current engine has ended.
void TsProcessor::finishRestore(bool clean) {
    auto back = engine_->takeStandIns();
    {
        const std::lock_guard<std::mutex> lock(mediaMu_);
        if (!clean) record_.merge(restored_);
        for (const auto& d : back)
            if (d) record_.captureFloppy(*d);
    }
    unserve();
    standInCds_.clear();
    standInDisks_.clear();
    restored_.clear();
    restoreWanted_ = false;
    restoreRan_ = false;
}

void TsProcessor::unserve() {
    for (const uint32_t id : standInCds_) cd_.serveFrom(nullptr, id);
}

void TsProcessor::capture(const ens::ts::Disk& d) {
    if (mode_ != MemoryMode::Project) return;  // DECISION A121: the Standalone records nothing
    const std::lock_guard<std::mutex> lock(mediaMu_);
    record_.captureFloppy(d);
}

// DECISION A116: every 60 s, once booted, the audio thread copies the RAM (requestRam) and the next
// tick that finds the copy (takeRam) saves it; 1800 ticks of the 30 Hz timer make the 60 s.
void TsProcessor::tick() {
    if (auto* done = sysexDone_.exchange(nullptr)) {  // DECISION A136: freed on the message thread
        delete done;
        sysexBusy_.store(false);
    }
    if (!engine_) return;
    drive_.poll();  // A119: a removed disk freed, a save's copy written; A129: floppies captured
    {
        const std::lock_guard<std::mutex> lock(mediaMu_);
        cd_.poll();  // A128: the sectors served to the record
    }
    // DECISION A129: once the restore has ended (a block has run and restoring is false), the stand-ins. The snapshot
    // is taken under stateMu_: one taken before it could be the old engine's, while a host's setStateInformation on
    // another thread starts a new restore that the snapshot would then finish early.
    if (restoreRan_) {
        const std::lock_guard<std::mutex> lock(stateMu_);
        const auto s = engine_->snapshot();
        if (restoreRan_ && s.frames > 0 && !s.restoring) finishRestore(s.restoreClean);
    }
    // DECISION A111 (as KS-32 A18): follow a program change tracked from the panel or MIDI (programSeq
    // moved), never the engine's own sends. followed_ is stored first, then `sound` is written, so a
    // processBlock that sees the new `sound` also finds the marker (and adopts the value instead of
    // sending it). A marker the host overwrote before any block took it is cleared here. While a marker
    // still waits for a block, no new follow starts (lastSeq_ stays, the next tick retries): a block that
    // read the older followed value just after a newer marker was stored would send it.
    if (int m = followed_.load(); m >= 0 && m != soundNow()) followed_.compare_exchange_strong(m, -1);
    const auto snap = engine_->snapshot();
    if (snap.program >= 0 && snap.programSeq != lastSeq_ && followed_.load() < 0) {
        lastSeq_ = snap.programSeq;
        if (auto* p = params_.getParameter("sound"); p != nullptr && soundNow() != snap.program) {
            followed_.store(snap.program);
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(snap.program)));
            p->endChangeGesture();
        }
    }
    if (ramPending_) {
        if (engine_->takeRam(ramBuf_)) {
            ramPending_ = false;
            saved(saveMemoryFile(settings_.memoryFile(), ramBuf_));
        }
    } else if (mode_ == MemoryMode::File && ++ticks_ >= 1800 && snap.booted) {  // DECISION A117: File only
        ticks_ = 0;
        engine_->requestRam();
        ramPending_ = true;
    }
}

void TsProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    rate_ = sampleRate;
    block_ = std::max(1, maximumExpectedSamplesPerBlock);
    auxL_.assign(static_cast<size_t>(block_), 0.0f);  // DECISION A109: Aux when its bus is off
    auxR_.assign(static_cast<size_t>(block_), 0.0f);
    // DECISION A134: the outputs' AC coupling, a one-pole high-pass at kDcHz.
    dcR_ = static_cast<float>(std::exp(-2.0 * juce::MathConstants<double>::pi * kDcHz / rate_));
    for (auto& d : dc_) d = {};
    midiOut_.ensureSize(4096);  // DECISION A133: a block's notes without growing; a long sysex reply grows it once
    if (engine_) engine_->prepare(rate_, block_);
}

// DECISION A134: y = x - x1 + R * y1 per channel.
void TsProcessor::DcBlock::run(float* x, int n, float r) {
    for (int i = 0; i < n; ++i) {
        const float y = x[i] - x1 + r * y1;
        x1 = x[i];
        y1 = y;
        x[i] = y;
    }
}

// DECISION A136: a .syx file. A send reads the whole file here (it is small: the TS's largest dump is 474 KB) and
// hands it over; the audio thread takes it at a block's start.
bool TsProcessor::sendSysex(const juce::File& f, juce::String& err) {
    err = {};
    if (sysexBusy_.load()) {
        err = "A SysEx file is still being sent.";
        return false;
    }
    if (!engine_ || !prepared()) {
        err = "The machine is not running.";
        return false;
    }
    juce::MemoryBlock mb;
    if (f.getSize() > 16 * 1024 * 1024 || !f.loadFileAsData(mb) || mb.getSize() == 0) {
        err = "Cannot read " + f.getFullPathName();
        return false;
    }
    const auto* b = static_cast<const uint8_t*>(mb.getData());
    if (b[0] != 0xF0) {
        err = "Not a SysEx file (it does not start with F0): " + f.getFullPathName();
        return false;
    }
    auto* bytes = new std::vector<uint8_t>(b, b + mb.getSize());
    sysexSize_.store(bytes->size());
    sysexSent_.store(0);
    sysexStop_.store(false);
    sysexBusy_.store(true);
    delete sysexIn_.exchange(bytes);  // empty: busy was false, so the last send was taken and freed
    return true;
}

float TsProcessor::sysexProgress() const {
    if (!sysexBusy_.load()) return -1;
    const size_t size = sysexSize_.load();
    return size == 0 ? 0.0f : static_cast<float>(sysexSent_.load()) / static_cast<float>(size);
}

void TsProcessor::stopSysex() { sysexStop_.store(true); }

// DECISION A136: fed while the engine holds less than 1 KB for the wire (A101), so the file goes at the wire's pace
// and the store keeps room for the host's notes, which queue behind it as on a MIDI cable. One message at a time:
// after an F7 the next F0 waits until the OS has read every byte, then 100 ms. Back to back, the OS NAKs a 74 KB
// program dump that follows another (TSD-1000.syx: one NAK, F0 0F 07 00 00 01 00 00 F7); one at a time, none.
void TsProcessor::feedSysex(int frames) {
    if (sysex_ == nullptr) {
        sysex_ = sysexIn_.exchange(nullptr);
        sysexPos_ = 0;
        sysexGap_ = 0;
        if (sysex_ == nullptr) return;
    }
    if (sysexGap_ > 0) {  // after a message: the OS reads it all, then the pause
        if (engine_->midiWaiting() > 0) return;
        sysexGap_ -= frames;
        if (sysexGap_ > 0 && !sysexStop_.load()) return;
        sysexGap_ = 0;
    }
    const size_t size = sysex_->size();
    if (sysexStop_.load() && sysexPos_ < size) {
        const uint8_t eox = 0xF7;
        if (sysexPos_ > 0 && (*sysex_)[sysexPos_ - 1] != 0xF7) engine_->midi(0, &eox, 1);
        sysexPos_ = size;
    }
    constexpr size_t kAhead = 1024;
    const size_t waiting = engine_->midiWaiting();
    if (sysexPos_ < size && waiting < kAhead) {
        const uint8_t* from = sysex_->data() + sysexPos_;
        size_t n = std::min(size - sysexPos_, kAhead - waiting);
        if (const uint8_t* eox = std::find(from, from + n, uint8_t{0xF7}); eox != from + n) {
            n = static_cast<size_t>(eox - from) + 1;
            sysexGap_ = std::max(1, static_cast<int>(rate_ / 10));  // 100 ms of frames
        }
        engine_->midi(0, from, static_cast<int>(n));
        sysexPos_ += n;
        sysexSent_.store(sysexPos_);
    }
    if (sysexPos_ == size && sysexDone_.load() == nullptr) {  // parked for tick() to free
        sysexDone_.store(sysex_);
        sysex_ = nullptr;
    }
}

// DECISION A133: the firmware's MIDI out goes to the host (the VST3's track, the Standalone's MIDI output) as whole
// messages at the block's start: the engine drains it once a block (A2), so finer timing is not known.
void TsProcessor::addMidiOut(void* self, const uint8_t* data, int size) {
    static_cast<TsProcessor*>(self)->midiOut_.addEvent(data, size, 0);
}

// DECISION A109: Main stereo; Aux stereo or disabled.
bool TsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto aux = layouts.getChannelSet(false, 1);
    return layouts.inputBuses.isEmpty() && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           (aux.isDisabled() || aux == juce::AudioChannelSet::stereo());
}

void TsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audioThread_.store(juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);  // DECISION A117
    auto main = getBusBuffer(buffer, false, 0);  // views of buffer's channels: no allocation
    auto aux = getBusBuffer(buffer, false, 1);
    if (!engine_ || main.getNumChannels() < 2 || auxL_.empty()) {  // no machine, or not prepared
        buffer.clear();
        midi.clear();  // DECISION A133: no MIDI out, and the input is not echoed
        return;
    }
    feedSysex(buffer.getNumSamples());  // DECISION A136: before the host's bytes of this block, which queue behind it
    // DECISION A111: parameter values cross as atomic raw values: no lock, no allocation.
    for (size_t i = 0; i < std::size(kAnalogParams); ++i)
        engine_->setAnalog(kAnalogParams[i].analog,
                           ens::app::analogRaw(kAnalogParams[i].analog, analogRaw_[i]->load()));
    // DECISION A111 (as KS-32 A18): a `sound` that tick() just followed (it holds the marker) is adopted,
    // not sent. Since tick() starts no follow while a marker waits, any other value is one already
    // requested or adopted (setProgram sends nothing) or a host request (sent). Remaining case: a panel
    // pick and a host change of `sound` inside one tick period can leave the host showing the followed
    // value while the machine plays the host's, until the next change of either.
    const int sound = soundNow();
    int marker = sound;
    if (followed_.compare_exchange_strong(marker, -1))
        engine_->adoptProgram(sound);
    else
        engine_->setProgram(sound);
    const int n = buffer.getNumSamples();
    float* const mainL = main.getWritePointer(0);
    float* const mainR = main.getWritePointer(1);
    if (aux.getNumChannels() >= 2) {
        // DECISION A108: every byte of every message, sysex and real-time included, as the MIDI IN jack.
        for (const auto m : midi) engine_->midi(m.samplePosition, m.data, m.numBytes);
        engine_->render(mainL, mainR, aux.getWritePointer(0), aux.getWritePointer(1), n);
        float* const out[4] = {mainL, mainR, aux.getWritePointer(0), aux.getWritePointer(1)};
        for (int c = 0; c < 4; ++c) dc_[c].run(out[c], n, dcR_);  // DECISION A134
        midi.swapWith(midiOut_);  // DECISION A133: the input is consumed; the output replaces it
        midiOut_.clear();
        return;
    }
    // DECISION A109: Aux off: rendered into the scratch pair and added into Main. A block longer than
    // prepared goes in pieces of the scratch size, each with its own messages (A108). A 0-sample block
    // (the VST3 wrapper makes them) is one piece of 0 frames, so its messages are not lost.
    const int piece = static_cast<int>(auxL_.size());
    for (int done = 0; done == 0 || done < n; done += piece) {
        const int k = std::min(piece, n - done);
        for (const auto m : midi)
            if (const int at = std::clamp(m.samplePosition, 0, std::max(0, n - 1)); at / piece == done / piece)
                engine_->midi(at - done, m.data, m.numBytes);
        engine_->render(mainL + done, mainR + done, auxL_.data(), auxR_.data(), k);
        juce::FloatVectorOperations::add(mainL + done, auxL_.data(), k);
        juce::FloatVectorOperations::add(mainR + done, auxR_.data(), k);
    }
    dc_[0].run(mainL, n, dcR_);  // DECISION A134
    dc_[1].run(mainR, n, dcR_);
    midi.swapWith(midiOut_);  // DECISION A133
    midiOut_.clear();
}

// DECISION A109: host bypass keeps the machine running and takes MIDI as processBlock does, so a note-off
// sent while bypassed is not lost; the output is silent.
void TsProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    processBlock(buffer, midi);
    buffer.clear();
}

// DECISION A117 (as KS-32 A21): once booted, the running RAM, whatever thread asks. On the audio thread
// (the one that last ran processBlock) no block runs during this call, so the machine is read directly:
// no wait, no gap, but a 512 KB copy that allocates there, which is the host's choice in calling getState
// on it. On any other thread the audio thread copies it (requestRam) and this waits at most 300 ms; if no
// block ran (the machine is not running either) it reads the machine under suspendProcessing. Before the
// boot ends, the start image (never a half-booted one); with no machine, the image a state gave, unchanged.
// The 60 s save (ramPending_) also uses requestRam/takeRam, but only in File mode, whose state has no RAM
// and which refuses copyToSharedMemory: the two users never meet in one instance.
std::vector<uint8_t> TsProcessor::ram() {
    if (!engine_ || !engine_->snapshot().booted) return startImage_;
    if (juce::Thread::getCurrentThreadId() == audioThread_.load(std::memory_order_relaxed))
        return engine_->machine().ramImage();
    std::vector<uint8_t> img;
    engine_->takeRam(img);  // a copy left by a request that timed out is stale: dropped
    engine_->requestRam();
    for (int i = 0; i < 60; ++i) {
        if (engine_->takeRam(img)) return img;
        juce::Thread::sleep(5);
    }
    suspendProcessing(true);
    img = engine_->machine().ramImage();
    suspendProcessing(false);
    return img;
}

// DECISION A117 (as KS-32 A20, A23): copyXmlToBinary of <TSState version="1" select="b000002000c020">,
// the parameter tree and, in Project mode, <RAM> (524 288 bytes, base64). `select` is the running
// selection once booted, else the one the start image came with. The Standalone's state has no RAM; its
// memory is the file. It takes the state mutex on every thread: a bounded wait behind a concurrent save
// or restart. processBlock itself never takes it.
void TsProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::XmlElement xml("TSState");
    xml.setAttribute("version", 1);
    const std::lock_guard<std::mutex> lock(stateMu_);
    const auto sel = select(engine_ ? engine_->snapshot() : ens::app::Snapshot{});
    if (!sel.empty())
        xml.setAttribute("select", juce::String::toHexString(sel.data(), static_cast<int>(sel.size()), 0));
    if (auto tree = params_.copyState().createXml()) xml.addChildElement(tree.release());
    if (mode_ == MemoryMode::Project) {
        if (const auto img = ram(); !img.empty())
            xml.createNewChildElement("RAM")->addTextElement(juce::Base64::toBase64(img.data(), img.size()));
        // DECISION A129 (Review Focus 3): while a restore is due or running, the media it restores, unchanged.
        const std::lock_guard<std::mutex> mediaLock(mediaMu_);
        const auto& media = restoreWanted_ ? restored_ : record_.media();
        if (!media.empty()) xml.addChildElement(mediaXml(media).release());
    }
    copyXmlToBinary(xml, dest);
}

// DECISION A117 (as KS-32 A20, A23): a blob that is not a TSState is ignored. Otherwise the parameters
// are replaced and a valid `select` sets `sound` to its program (the state may predate the timer's
// follow), written into the restored tree so no extra host edit is sent while a project loads. In Project
// mode a version 1 RAM of 524 288 bytes restarts the machine from it with that selection; with no ROMs it
// waits as the start image. No RAM, a wrong size or another version leaves the machine as it is.
void TsProcessor::setStateInformation(const void* data, int size) {
    // DECISION A121: only a plugin restores a state; the Standalone starts with the controls at their
    // defaults and the sound its memory file boots on, whatever JUCE's wrapper saved.
    if (mode_ == MemoryMode::File) return;
    const auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("TSState")) return;
    juce::ValueTree tree;
    if (const auto* t = xml->getChildByName(params_.state.getType())) tree = juce::ValueTree::fromXml(*t);

    juce::MemoryBlock hex;
    hex.loadFromHexString(xml->getStringAttribute("select"));
    const auto* b = static_cast<const uint8_t*>(hex.getData());
    const auto n = hex.getSize();
    std::vector<uint8_t> sel;
    if (n >= 2 && n <= 8 && (b[n - 2] & 0xF0) == 0xC0 && b[n - 1] < 0x80) {  // ends Cn pp; setSelect checks the rest
        sel.assign(b, b + n);
        if (!tree.isValid()) tree = params_.copyState();
        auto sound = tree.getChildWithProperty("id", "sound");
        if (!sound.isValid()) {
            sound = juce::ValueTree("PARAM");
            sound.setProperty("id", "sound", nullptr);
            tree.appendChild(sound, nullptr);
        }
        sound.setProperty("value", static_cast<float>(b[n - 1]), nullptr);
    }
    if (tree.isValid()) params_.replaceState(tree);

    std::vector<uint8_t> img;
    if (const auto* r = xml->getChildByName("RAM");
        r != nullptr && mode_ == MemoryMode::Project && xml->getIntAttribute("version") == 1) {
        juce::MemoryOutputStream out;
        if (juce::Base64::convertFromBase64(out, r->getAllSubText()) &&
            out.getDataSize() == static_cast<size_t>(kMemoryBytes)) {
            const auto* p = static_cast<const uint8_t*>(out.getData());
            img.assign(p, p + out.getDataSize());
        }
    }
    // DECISION A129: the media go with the RAM; a medium that is not whole is dropped (Review Focus 5).
    auto media = img.empty() ? std::vector<SavedMedium>() : parseMedia(xml->getChildByName("Media"));
    const std::lock_guard<std::mutex> lock(stateMu_);
    if (!img.empty()) {
        startImage_ = std::move(img);
        startSelect_ = std::move(sel);
        haveStart_ = true;
        // DECISION A129: the record starts again from what the restore's auto-load reads.
        unserve();  // before restored_ changes; restart tells the old stand-ins from the user's media
        restored_ = std::move(media);
        restoreWanted_ = !restored_.empty();
        {
            const std::lock_guard<std::mutex> m(mediaMu_);
            record_.clear();
        }
        if (engine_) restart(Ram::Start, true);  // with no ROMs, setRomFolder starts from it
    } else if (!sel.empty()) {
        // A state with no RAM arriving before the first block: its select is the one the next boot
        // replays, and with no ROMs it waits for setRomFolder's restart. Once booted the running
        // select stays what a state saves, and the engine sends this one when `sound` asks for it.
        if (!engine_ || !engine_->snapshot().booted) startSelect_ = sel;
        if (engine_) {
            suspendProcessing(true);
            engine_->setSelect(sel.data(), static_cast<int>(sel.size()));
            suspendProcessing(false);
        }
    }
}

// The select a state saves and a Keep or File restart keeps (A105, A117): the running one once booted,
// else the one the start image came with.
std::vector<uint8_t> TsProcessor::select(const ens::app::Snapshot& snap) const {
    if (snap.booted && snap.selectLen > 0) return {snap.select.begin(), snap.select.begin() + snap.selectLen};
    return startSelect_;
}

// `sound` as a program; a non-finite value (a project file is not trusted) reads as 0.
int TsProcessor::soundNow() const {
    const float v = sound_->load();
    return std::isfinite(v) ? static_cast<int>(std::clamp(v, 0.0f, 127.0f)) : 0;
}

// DECISION A117: the instance's RAM to the shared file, temporary file then replace (A116).
bool TsProcessor::copyToSharedMemory(juce::String& err) {
    if (mode_ == MemoryMode::File) {
        err = "The Standalone saves the memory file itself.";
        return false;
    }
    if (!engine_ || !engine_->snapshot().booted) {
        err = "The machine has not finished booting. Try again when it has.";
        return false;
    }
    std::vector<uint8_t> img;
    {
        const std::lock_guard<std::mutex> lock(stateMu_);
        img = ram();
    }
    if (!saved(saveMemoryFile(settings_.memoryFile(), img))) {
        err = memoryError_;
        return false;
    }
    return true;
}

juce::AudioProcessorEditor* TsProcessor::createEditor() { return new PanelEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TsProcessor(); }
