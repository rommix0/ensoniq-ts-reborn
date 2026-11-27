#pragma once
// The floppy drive's file side (A119): which image file the disk in the drive came from, opening, saving and
// ejecting it through the engine (A118), and whether the OS has written to it since. Owned by TsProcessor,
// message thread only. The dialogs (A120) are here too, so the editor and the Standalone's window share them;
// the checks call the plain methods, never a dialog.
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "ts/disk_image.h"

class TsProcessor;
namespace ens::app {
class Engine;
}

class DiskDrive {
public:
    // A save's result, on the message thread once the file is written or the save failed.
    using Done = std::function<void(bool ok, const juce::String& err)>;

    explicit DiskDrive(TsProcessor& p) : proc_(p) {}

    // Decodes the file and puts it in the drive; on failure (err) the drive keeps what it had.
    bool open(const juce::File& f, juce::String& err);
    // An unformatted disk, "Untitled (blank)" until saved as a file. False when the drive is busy.
    bool newBlank(ens::ts::Density d);
    // To the file it came from, in its format, through <file>.tmp. False with err at once when there is no
    // disk, no file, the file is read-only or a save is running; otherwise true, and the file is written when
    // the copy arrives (the tick after the next block), `done` reporting the result. Open, New or Eject before
    // then gives the save up (`done` false); the copy needs the audio running.
    bool save(juce::String& err, Done done = {});
    // As save, to f in the format its extension names (formatForName); f becomes the drive's file.
    bool saveAs(const juce::File& f, juce::String& err, Done done = {});
    bool eject();  // false when the drive is busy
    void setWriteProtected(bool p);
    bool writeProtected() const { return protect_; }

    bool canSave() const;  // a disk from a file that can be written
    // The engine's writes count differs from the one at the last open, new or save (A119).
    bool modified() const;
    // The file name, plus " (modified)"; "Untitled (blank)" for a new disk; empty with no disk.
    juce::String slotText() const;

    // TsProcessor::tick: frees the disk a swap took out and writes a save whose copy has come. DECISION A129: in
    // Project mode it also asks for a copy when the OS has read new blocks, and hands each copy and each disk that
    // left the drive to TsProcessor::capture.
    void poll();

    // The dialogs (A119, A120), message thread. The drive's menu, at the control clicked.
    void showMenu(juce::Component* target);
    void openAsked(const juce::File& f);  // a file dropped on the panel: asks first, then opens
    // `then` runs at once when the disk has no unsaved writes, else once the user saved it (Save, or Save As
    // for a disk with no writable file) or chose Don't Save; Cancel or a failed save drops it.
    void askThen(std::function<void()> then);
    // askThen would wait for the save in flight rather than ask: the audio runs and nothing waits yet (A120).
    bool waitsForSave() const;

private:
    struct Save {
        juce::File file;
        ens::ts::ImageFormat format = ens::ts::ImageFormat::Raw;
        std::vector<uint8_t> header;
        uint32_t swaps = 0;  // the drive's swap count of the disk being saved
        Done done;
        bool waited = false;  // an askThen action is chained onto `done`
    };
    ens::app::Engine* engine();   // the engine after follow(); null with no ROMs
    void follow();                // a restarted engine: counts start again, a pending save asks again
    uint32_t swapsNow() const;    // swaps_ as follow() would set it
    bool insert(std::unique_ptr<ens::ts::Disk> d);
    bool startSave(Save s, juce::String& err);
    bool askCopy(ens::app::Engine* e);
    juce::String notNow() const;  // why an insert or eject was not taken
    void finish(bool ok, const juce::String& err);
    void saveThen(std::function<void()> then);
    void chooseOpen();
    void chooseSaveAs(std::function<void()> then);
    static void showError(const juce::String& err);

    TsProcessor& proc_;
    uint32_t gen_ = 0;    // the processor's engine generation the counts below belong to
    uint32_t swaps_ = 0;  // inserts and ejects this drive made in that engine (Snapshot::diskSwaps)
    bool in_ = false, protect_ = false;
    ens::ts::Density density_ = ens::ts::Density::Double;
    int cylinders_ = ens::ts::Disk::kCylinders;  // DECISION T30: more on an HFE super disk
    juce::File file_;  // empty for a new disk
    ens::ts::ImageFormat format_ = ens::ts::ImageFormat::Raw;
    std::vector<uint8_t> header_;  // as decoded, kept for saving in the same format
    uint32_t baseWrites_ = 0;      // Disk::writes() at the last open, new or save
    std::unique_ptr<Save> save_;   // the save waiting for its copy
    bool copyAsked_ = false;       // the engine holds a copy request of ours
    // A129: Snapshot::diskReads and diskSwaps when a copy was last asked for the record; ~0u after a restart.
    uint32_t readsSeen_ = ~0u, swapsSeen_ = ~0u;
    bool forRecord_ = false;  // A129: the copy asked is the record's, not a save's
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(DiskDrive)
    JUCE_DECLARE_NON_COPYABLE(DiskDrive)
};
