#include "CdDrive.h"

#include <cstring>

#include "Processor.h"
#include "ts/sector_port.h"

using ens::ts::SectorPort;

namespace {

// DECISION A127: an .iso is its own sectors; a .cue is parsed (one TRACK 01 MODE1/2352 from INDEX 01 00:00:00,
// cd_image.h) and its FILE is taken as written (relative to the .cue's folder), else, missing, as that file name in
// the .cue's folder matched without case: rippers write their own path (CDR-13's names E:\...\DLM-CDR13.BIN beside
// a dlm-cdr13.bin), as tools/common/cd_file.cpp does. Anything else is refused naming what is supported.
bool openImage(const juce::File& f, ens::ts::CdLayout& layout, juce::File& image, uint32_t& sectors,
               juce::String& err) {
    std::string why;
    if (!f.existsAsFile()) {
        err = "Cannot read " + f.getFullPathName();
        return false;
    }
    if (f.hasFileExtension("iso")) {
        layout = ens::ts::isoLayout(f.getFullPathName().toStdString());
        image = f;
    } else if (f.hasFileExtension("cue")) {
        if (!ens::ts::parseCue(f.loadFileAsString().toStdString(), layout, why)) {
            err = "Not inserted, " + juce::String::fromUTF8(why.c_str()) + ": " + f.getFullPathName();
            return false;
        }
        const auto named = juce::String::fromUTF8(layout.file.c_str());
        image = juce::File::isAbsolutePath(named) ? juce::File(named) : f.getSiblingFile(named);
        if (!image.existsAsFile()) {
            const std::string want = ens::ts::baseName(layout.file);
            image = juce::File();
            for (const auto& c : f.getParentDirectory().findChildFiles(juce::File::findFiles, false))
                if (ens::ts::sameNameNoCase(c.getFileName().toStdString(), want)) {
                    image = c;
                    break;
                }
            if (image == juce::File()) {
                err = "Not inserted, the image file " + juce::String::fromUTF8(want.c_str()) +
                      " is not beside " + f.getFullPathName();
                return false;
            }
        }
    } else {
        err = "Not a CD image (use .iso or .cue): " + f.getFullPathName();
        return false;
    }
    if (!ens::ts::sectorCount(layout, static_cast<uint64_t>(image.getSize()), sectors, why)) {
        err = "Not inserted, " + juce::String::fromUTF8(why.c_str()) + ": " + image.getFullPathName();
        return false;
    }
    return true;
}

// DECISION A128: the file is opened for each sector, so an image moved, deleted or on a drive pulled out fails at the
// next sector (Review Focus 1), on Linux too, where an open file stays readable after it is deleted.
// ponytail: an open per 2048 bytes; keep the stream open across a burst of asks if a load ever measures slow.
bool readSector(const juce::File& f, uint64_t at, uint8_t* out) {
    juce::FileInputStream in(f);
    return in.openedOk() && in.setPosition(static_cast<juce::int64>(at)) &&
           in.read(out, SectorPort::kBytes) == SectorPort::kBytes;
}

void showError(const juce::String& err) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "CD-ROM drive", err);
}

}  // namespace

// DECISION A128: the reader thread polls the engine's port: a reply as soon as an ask is seen, else a 1 ms wait (the
// audio side may not signal, T29). One ask is answered at a time; the next comes once the machine has taken it.
class CdDrive::Reader : public juce::Thread {
public:
    explicit Reader(CdDrive& d) : juce::Thread("TS Reborn CD reader"), d_(d) {}
    void run() override {
        while (!threadShouldExit())
            if (!d_.serveOne()) wait(1);
    }

private:
    CdDrive& d_;
};

CdDrive::CdDrive(TsProcessor& p) : proc_(p), reader_(std::make_unique<Reader>(*this)) {}

CdDrive::~CdDrive() { reader_->stopThread(2000); }

void CdDrive::setEngine(ens::app::Engine* e) {
    const std::lock_guard<std::mutex> lock(mu_);
    port_ = e != nullptr ? e->cdPort() : nullptr;
    ++portGen_;
}

// The reader thread. The file is read outside the lock, so a slow drive never holds up the message thread; the reply
// goes only to the port it was read for (setEngine may have replaced it meanwhile, and the old engine is then freed).
bool CdDrive::serveOne() {
    uint8_t buf[SectorPort::kBytes] = {};
    uint32_t medium = 0, lba = 0, gen = 0;
    bool ok = false;
    Source s;
    {
        const std::lock_guard<std::mutex> lock(mu_);
        if (port_ == nullptr || !port_->pending(medium, lba)) return false;
        gen = portGen_;
        const auto it = sources_.find(medium);
        if (it == sources_.end()) {  // a medium no longer served: the ask fails (MEDIUM ERROR, T28)
            port_->reply(medium, lba, nullptr, false);
            return true;
        }
        s = it->second;
        if (s.saved != nullptr) {  // a stand-in: copied under the lock, its owner changes it only after serveFrom
            const auto b = s.saved->blocks.find(lba);
            ok = b != s.saved->blocks.end() && b->second.size() == sizeof buf;
            if (ok) std::memcpy(buf, b->second.data(), sizeof buf);
        }
    }
    if (s.saved == nullptr) ok = lba < s.sectors && readSector(s.file, ens::ts::sectorOffset(s.layout, lba), buf);
    const std::lock_guard<std::mutex> lock(mu_);
    if (gen != portGen_) return true;
    port_->reply(medium, lba, buf, ok);
    // DECISION A129, A138: what the OS read goes to the record, in both modes.
    if (ok) served_.push_back({s.sectors, lba, {buf, buf + sizeof buf}});
    return true;
}

// DECISION A128: medium ids count up from 1 per drive (so per processor) and are never reused, also for a refused
// insert: a late reply for an old medium must never match a new one (T29). The source is known to the reader before
// the engine can ask for it.
bool CdDrive::insert(const juce::File& f, juce::String& err) {
    err = {};
    Source s;
    if (!openImage(f, s.layout, s.file, s.sectors, err)) return false;
    auto* e = proc_.engine();
    if (e == nullptr || !proc_.prepared()) {  // with no audio running the insert would never apply (as A119)
        err = notNow();
        return false;
    }
    const uint32_t id = newMedium();
    {
        const std::lock_guard<std::mutex> lock(mu_);
        sources_[id] = s;  // ponytail: sources are kept for the drive's life, a path each; erase on eject if it matters
    }
    if (!e->insertCd(id, s.sectors)) {
        const std::lock_guard<std::mutex> lock(mu_);
        sources_.erase(id);
        err = notNow();
        return false;
    }
    file_ = f;
    if (!reader_->isThreadRunning()) reader_->startThread();
    return true;
}

bool CdDrive::eject() {
    if (file_ == juce::File()) return true;
    auto* e = proc_.engine();
    if (e == nullptr || !proc_.prepared() || !e->insertCd(0, 0)) return false;
    file_ = juce::File();
    return true;
}

// DECISION A128: while the restore runs (A131) the slot waits for it; saying so is kinder than "busy".
juce::String CdDrive::notNow() const {
    auto* e = proc_.engine();
    if (e == nullptr) return "The machine is not running: choose the ROM folder first.";
    if (!proc_.prepared()) return "The machine is not running (no audio device), so the CD cannot be changed.";
    if (e->snapshot().restoring) return "The machine is restoring the project's sounds. Try again when it has done.";
    return "The drive is busy. Try again.";
}

void CdDrive::serveFrom(const ens::app::SavedMedium* m, uint32_t medium) {
    {
        const std::lock_guard<std::mutex> lock(mu_);
        if (m == nullptr) {
            sources_.erase(medium);
            return;
        }
        Source s;
        s.saved = m;
        s.sectors = m->size;
        sources_[medium] = s;
    }
    if (!reader_->isThreadRunning()) reader_->startThread();
}

void CdDrive::poll() {
    std::vector<Served> got;
    {
        const std::lock_guard<std::mutex> lock(mu_);
        got.swap(served_);
    }
    for (const auto& s : got) proc_.record().captureCd(s.size, s.lba, s.data.data());
}

// The dialogs. Anything that runs later (the chooser's result) checks a weak reference first.

void CdDrive::insertAsked(const juce::File& f) {
    juce::String err;
    if (!insert(f, err)) showError(err);
}

void CdDrive::ejectAsked() {
    if (!eject()) showError(notNow());
}

void CdDrive::chooseInsert() {
    const auto from = file_ != juce::File() ? file_.getParentDirectory()
                                            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    // Both cases: Linux matches the pattern's case (as A119).
    chooser_ = std::make_unique<juce::FileChooser>("Insert a CD image (.iso or .cue)", from, "*.iso;*.ISO;*.cue;*.CUE");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self = juce::WeakReference<CdDrive>(this)](const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (self != nullptr && f != juce::File()) self->insertAsked(f);
                          });
}
