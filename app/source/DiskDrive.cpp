#include "DiskDrive.h"

#include <utility>

#include "MemoryFile.h"
#include "Processor.h"

using ens::ts::Density;
using ens::ts::Disk;

namespace {

// The largest image file (DECISION T30: an HFE super disk, 255 HD cylinders, is 12.8 MB; a Giebler HD file holding
// every block is 1 638 913 bytes); a bigger one is not read into memory, and decodeImage refuses its empty stand-in
// naming the sizes it takes.
constexpr juce::int64 kMaxImageBytes = 16 * 1024 * 1024;

// DECISION A119: the chooser lists the extensions formatForName knows, both cases (Linux matches case).
const char* const kImageFiles = "*.img;*.IMG;*.gkh;*.GKH;*.ede;*.EDE;*.eda;*.EDA;*.edt;*.EDT;*.hfe;*.HFE";

}  // namespace

// DECISION A119: the drive counts its own accepted inserts and ejects (`swaps_`) in the engine they went to;
// TsProcessor::restart carries the disk into the new engine as its first insert, so a new generation starts
// at 1 with a disk in and 0 without.
uint32_t DiskDrive::swapsNow() const { return gen_ == proc_.engineGeneration() ? swaps_ : (in_ ? 1u : 0u); }

void DiskDrive::follow() {
    const uint32_t g = proc_.engineGeneration();
    if (g == gen_) return;
    const bool saving = save_ && save_->swaps == swaps_;  // the disk being saved is still in
    gen_ = g;
    swaps_ = in_ ? 1u : 0u;
    copyAsked_ = false;  // the old engine's copy request went with it
    readsSeen_ = swapsSeen_ = ~0u;  // A129: and so did a capture's copy: ask again
    forRecord_ = false;
    auto* e = proc_.engine();
    if (e == nullptr) return;
    e->setWriteProtect(protect_);  // a request the old engine had not applied yet
    if (save_) {
        save_->swaps = swaps_;
        if (!saving || !askCopy(e)) finish(false, "The disk changed before it was saved.");
    }
}

// One copy request at a time in the engine; copyAsked_ until poll() has taken its answer.
bool DiskDrive::askCopy(ens::app::Engine* e) {
    if (copyAsked_) return true;  // an earlier request, not served yet (audio stopped), answers this save too
    copyAsked_ = e->requestDisk(Disk::blank(density_, cylinders_));
    return copyAsked_;
}

ens::app::Engine* DiskDrive::engine() {
    follow();
    return proc_.engine();
}

// DECISION A118: the disk the last swap took out is freed here, before the next swap, so that swap can apply.
bool DiskDrive::insert(std::unique_ptr<Disk> d) {
    auto* e = engine();
    // DECISION A119: with no audio running the swap would never be applied; refuse it (notNow says why).
    if (e == nullptr || !proc_.prepared()) return false;
    if (const auto out = e->takeRemoved()) proc_.capture(*out);  // DECISION A129: its last reads
    if (!e->insertDisk(std::move(d))) return false;
    ++swaps_;
    // DECISION A119: a save waiting for its copy is given up when the disk changes (it may never come while
    // the audio is stopped, and would hold off every later save).
    if (save_) finish(false, "The disk was changed before it was saved.");
    return true;
}

juce::String DiskDrive::notNow() const {
    if (proc_.engine() == nullptr) return "The machine is not running: choose the ROM folder first.";
    // DECISION A128: while the restore runs (A131) the slots wait for it; saying so is kinder than "busy".
    if (proc_.prepared() && proc_.engine()->snapshot().restoring)
        return "The machine is restoring the project's sounds. Try again when it has done.";
    return proc_.prepared() ? "The drive is busy. Try again."
                            : "The machine is not running (no audio device), so the disk cannot be changed.";
}

// DECISION A119: the format comes from the content (decodeImage), never the name; a raw file's size decides
// the density. Opening does not reset the machine: the OS sees a disk change.
bool DiskDrive::open(const juce::File& f, juce::String& err) {
    err = {};
    std::vector<uint8_t> bytes;
    if (!f.existsAsFile()) {
        err = "Cannot read " + f.getFullPathName();
        return false;
    }
    if (f.getSize() <= kMaxImageBytes) {
        juce::MemoryBlock mb;
        if (!f.loadFileAsData(mb)) {
            err = "Cannot read " + f.getFullPathName();
            return false;
        }
        const auto* b = static_cast<const uint8_t*>(mb.getData());
        bytes.assign(b, b + mb.getSize());
    }
    ens::ts::DecodedImage im;
    std::string why;
    if (!ens::ts::decodeImage(bytes, im, why)) {
        err = "Not opened, " + juce::String(why) + ": " + f.getFullPathName();
        return false;
    }
    if (engine() == nullptr) {
        err = "The machine is not running: choose the ROM folder first.";
        return false;
    }
    im.disk->setWriteProtected(protect_);
    const uint32_t writes = im.disk->writes();
    const Density density = im.disk->density();
    const int cylinders = im.disk->cylinders();
    if (!insert(std::move(im.disk))) {
        err = notNow();
        return false;
    }
    in_ = true;
    density_ = density;
    cylinders_ = cylinders;
    file_ = f;
    format_ = im.format;
    header_ = std::move(im.header);
    baseWrites_ = writes;
    return true;
}

bool DiskDrive::newBlank(Density d) {
    auto disk = Disk::blank(d);
    disk->setWriteProtected(protect_);
    if (!insert(std::move(disk))) return false;
    in_ = true;
    density_ = d;
    cylinders_ = Disk::kCylinders;
    file_ = juce::File();
    format_ = ens::ts::ImageFormat::Raw;
    header_.clear();
    baseWrites_ = 0;
    return true;
}

bool DiskDrive::eject() {
    if (!in_) return true;
    if (!insert(nullptr)) return false;
    in_ = false;
    file_ = juce::File();
    header_.clear();
    return true;
}

// DECISION A119: the tick is the drive's, not the disk's: it applies to the disk in and to each disk put in,
// through the engine's block start (setWriteProtect), so the audio thread is the only one touching the disk.
void DiskDrive::setWriteProtected(bool p) {
    protect_ = p;
    if (auto* e = engine()) e->setWriteProtect(p);
}

bool DiskDrive::canSave() const { return in_ && file_ != juce::File() && file_.hasWriteAccess(); }

// DECISION A119: modified once the engine runs this drive's latest disk (its swap count matches) and that
// disk's writes count has moved from the one at the last open, new or save. Read from the snapshot, as the
// display is.
bool DiskDrive::modified() const {
    auto* e = proc_.engine();
    if (!in_ || e == nullptr) return false;
    const auto s = e->snapshot();
    return s.diskIn && s.diskSwaps == swapsNow() && s.diskWrites != baseWrites_;
}

juce::String DiskDrive::slotText() const {
    if (!in_) return {};
    const bool m = modified();
    if (file_ == juce::File()) return m ? "Untitled (modified)" : "Untitled (blank)";
    return file_.getFileName() + (m ? " (modified)" : "");
}

bool DiskDrive::save(juce::String& err, Done done) {
    err = {};
    if (!in_)
        err = "No disk in the drive.";
    else if (file_ == juce::File())
        err = "This disk has no file yet: use Save Disk Image As...";
    else if (!file_.hasWriteAccess())
        err = "The file cannot be written (read-only): use Save Disk Image As... " + file_.getFullPathName();
    if (err.isNotEmpty()) return false;
    return startSave({file_, format_, header_, 0, std::move(done)}, err);
}

// DECISION A119: Save As takes the format from the extension typed (formatForName: .gkh GKH, .ede .eda .edt
// Giebler, .hfe HFE, anything else raw), keeping the header read only when the format stays the same.
bool DiskDrive::saveAs(const juce::File& f, juce::String& err, Done done) {
    err = {};
    if (!in_) {
        err = "No disk in the drive.";
        return false;
    }
    if (f.isDirectory() || !f.hasWriteAccess()) {
        err = "The file cannot be written: " + f.getFullPathName();
        return false;
    }
    const auto format = ens::ts::formatForName(f.getFileName().toStdString());
    return startSave({f, format, format == format_ ? header_ : std::vector<uint8_t>(), 0, std::move(done)}, err);
}

// DECISION A118, A119: the copy is asked of the audio thread (a scratch of this disk's density) and taken
// by poll() on the processor's 30 Hz timer; the message thread never waits for it.
bool DiskDrive::startSave(Save s, juce::String& err) {
    auto* e = engine();
    if (save_) {
        err = "A save is already running.";
    } else if (e == nullptr || !proc_.prepared()) {
        err = "The machine is not running (no audio device), so the disk cannot be copied.";
    } else {
        // DECISION A129: a copy the record asked for and the engine already made may predate this save: the record
        // keeps it, and the save asks anew. One not made yet is made after this, so it serves the save.
        std::unique_ptr<Disk> made;
        bool ok = false;
        uint32_t at = 0;
        if (std::exchange(forRecord_, false) && copyAsked_ && e->takeDisk(made, ok, at)) {
            copyAsked_ = false;
            if (ok) proc_.capture(*made);
        }
        if (!askCopy(e)) err = "The drive is busy. Try again.";
    }
    if (err.isNotEmpty()) return false;
    s.swaps = swaps_;
    save_ = std::make_unique<Save>(std::move(s));
    return true;
}

void DiskDrive::poll() {
    auto* e = engine();
    if (e == nullptr) return;
    // DECISION A118: freed on the message thread. DECISION A129: a disk leaving the drive is captured first.
    if (const auto out = e->takeRemoved()) proc_.capture(*out);
    // DECISION A129: on the first tick after the OS read new blocks (or another disk went in), a copy for the record.
    // While the restore runs the stand-ins own the drive and the copy slot waits (A131): nothing is asked.
    // The gap: a block the OS writes between its read and the copy is kept as written.
    if (!copyAsked_ && proc_.memoryMode() == MemoryMode::Project) {
        const auto s = e->snapshot();
        if (!s.restoring && s.diskIn && (s.diskReads != readsSeen_ || s.diskSwaps != swapsSeen_) && askCopy(e)) {
            readsSeen_ = s.diskReads;
            swapsSeen_ = s.diskSwaps;
            forRecord_ = true;
        }
    }
    std::unique_ptr<Disk> copy;
    bool ok = false;
    uint32_t at = 0;
    if (!copyAsked_ || !e->takeDisk(copy, ok, at)) return;
    copyAsked_ = false;
    forRecord_ = false;
    if (ok) proc_.capture(*copy);  // DECISION A129: a save's copy too; capture keeps only new blocks
    if (!save_) return;  // the save it was for, if any, was given up: dropped
    if (at != save_->swaps || !ok) {
        // A copy of another disk (the one to save still waiting to go in), or a scratch of the density before:
        // ask again while the disk to save is in, else the save fails.
        if (save_->swaps == swaps_ && at <= swaps_ &&
            (at != save_->swaps || copy->density() != density_ || copy->cylinders() != cylinders_) &&
            askCopy(e))
            return;
        finish(false, at != save_->swaps ? "The disk changed before it was saved." : "The disk could not be copied.");
        return;
    }
    // DECISION A119: <file>.tmp, then replace (the memory file's way, A116): a failed save keeps the old file.
    // Giebler files get their bitmap from the disk's contents (encodeImage).
    const auto bytes = ens::ts::encodeImage(*copy, save_->format, save_->header);
    if (bytes.empty()) {  // DECISION T30: .img, .gkh and Giebler files hold 80 cylinders
        finish(false, "This disk has " + juce::String(copy->cylinders()) +
                          " cylinders: save it as an .hfe file (Save Disk Image As...).");
        return;
    }
    if (!saveMemoryFile(save_->file, bytes)) {
        finish(false, "Disk image not saved: " + save_->file.getFullPathName());
        return;
    }
    if (save_->swaps == swaps_) {  // still in the drive: the file it is saved in is its file now
        file_ = save_->file;
        format_ = save_->format;
        header_ = save_->header;
        baseWrites_ = copy->writes();
    }
    finish(true, {});
}

void DiskDrive::finish(bool ok, const juce::String& err) {
    const auto done = std::move(save_->done);
    save_.reset();
    if (done) done(ok, err);
}

// The dialogs. Menu items capture `this`: a popup menu closes with its target, which the processor (and so
// the drive) outlives. Anything later (an alert, a chooser, a save's result) checks a weak reference first.

void DiskDrive::showError(const juce::String& err) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Disk drive", err);
}

// DECISION A119: the menu of the drive drawn on the panel.
void DiskDrive::showMenu(juce::Component* target) {
    const auto fresh = [this](Density d) {
        askThen([this, d] {
            if (!newBlank(d)) showError(notNow());
        });
    };
    juce::PopupMenu m;
    m.addSectionHeader("Disk drive");
    m.addItem("Open Disk Image...", [this] { askThen([this] { chooseOpen(); }); });
    m.addItem("New Blank Disk (DD)", [fresh] { fresh(Density::Double); });
    m.addItem("New Blank Disk (HD)", [fresh] { fresh(Density::High); });
    m.addSeparator();
    m.addItem("Save Disk Image", canSave(), false, [this] { saveThen({}); });
    m.addItem("Save Disk Image As...", in_, false, [this] { chooseSaveAs({}); });
    m.addItem("Eject", in_, false, [this] {
        askThen([this] {
            if (!eject()) showError(notNow());
        });
    });
    m.addSeparator();
    m.addItem("Write Protected", true, protect_, [this] { setWriteProtected(!protect_); });
    // DECISION A128: the CD drive has no place on the panel (the TS-10 has only the rear SCSI connector), so its
    // menu is a submenu here: the CD's name (greyed), Insert and Eject.
    auto& cd = proc_.cdDrive();
    juce::PopupMenu cdMenu;
    cdMenu.addItem(cd.name().isEmpty() ? juce::String("No CD") : cd.name(), false, false, nullptr);
    cdMenu.addItem("Insert CD Image...", [&cd] { cd.chooseInsert(); });
    cdMenu.addItem("Eject CD", cd.name().isNotEmpty(), false, [&cd] { cd.ejectAsked(); });
    m.addSeparator();
    m.addSubMenu("CD-ROM (SCSI ID 4)", cdMenu);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target));
}

void DiskDrive::openAsked(const juce::File& f) {
    askThen([this, f] {
        juce::String err;
        if (!open(f, err)) showError(err);
    });
}

// DECISION A120: Open, New, Eject, a dropped file and the Standalone's close ask first when the disk has
// unsaved writes: Save, Don't Save or Cancel.
// DECISION A120: while a save is in flight (its copy not yet arrived) the disk still reads as modified; the
// action then waits for that save instead of asking again, runs when it succeeded and is dropped when it
// failed (its error is shown by the save's own handler), so the user's writes are never given up unasked.
// Only one action waits, and only while the audio runs: a save whose copy may never come (the device stopped,
// or the host stopped calling processBlock) must not swallow every later close, so the next one asks.
bool DiskDrive::waitsForSave() const { return save_ && !save_->waited && proc_.prepared(); }

void DiskDrive::askThen(std::function<void()> then) {
    if (!modified()) {
        then();
        return;
    }
    if (waitsForSave()) {
        save_->waited = true;
        save_->done = [prev = std::move(save_->done), then](bool ok, const juce::String& e) {
            if (prev) prev(ok, e);
            if (ok) then();
        };
        return;
    }
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Disk drive")
                             .withMessage("The OS has written to the disk since it was saved (" + slotText() +
                                          "). Save it first?")
                             .withButton("Save")
                             .withButton("Don't Save")
                             .withButton("Cancel");
    juce::AlertWindow::showAsync(options, [self = juce::WeakReference<DiskDrive>(this), then](int result) {
        if (self == nullptr) return;
        if (result == 1)
            self->saveThen(then);
        else if (result == 2)
            then();
    });
}

// Save, or Save As when the disk has no file it can be written to; `then` once the file is written.
void DiskDrive::saveThen(std::function<void()> then) {
    if (!canSave()) {
        chooseSaveAs(std::move(then));
        return;
    }
    juce::String err;
    if (!save(err, [then](bool ok, const juce::String& e) {
            if (!ok)
                showError(e);
            else if (then)
                then();
        }))
        showError(err);
}

void DiskDrive::chooseOpen() {
    const auto from = file_ != juce::File() ? file_.getParentDirectory()
                                            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Open a disk image", from, kImageFiles);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self = juce::WeakReference<DiskDrive>(this)](const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              juce::String err;
                              if (self != nullptr && f != juce::File() && !self->open(f, err)) showError(err);
                          });
}

void DiskDrive::chooseSaveAs(std::function<void()> then) {
    const auto from = file_ != juce::File()
                          ? file_
                          : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.img");
    chooser_ = std::make_unique<juce::FileChooser>("Save the disk image (.img, .gkh, .ede, .eda or .hfe)", from,
                                                   kImageFiles);
    chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [self = juce::WeakReference<DiskDrive>(this), then](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (self == nullptr || f == juce::File()) return;
            juce::String err;
            if (!self->saveAs(f, err, [then](bool ok, const juce::String& e) {
                    if (!ok)
                        showError(e);
                    else if (then)
                        then();
                }))
                showError(err);
        });
}
