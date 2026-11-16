#pragma once
// The CD-ROM drive at SCSI ID 4 (Plan 7, A127, A128): which image is in it, insert and eject through the engine's
// single slot, and a reader thread that answers the engine's sector asks from the image file (or from a project's
// recorded sectors, a stand-in). Owned by TsProcessor; every method is for the message thread. The CD is not part of
// the plugin state (A128).
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "app/media_record.h"
#include "ts/cd_image.h"

class TsProcessor;
namespace ens::app {
class Engine;
}
namespace ens::ts {
class SectorPort;
}

class CdDrive {
public:
    explicit CdDrive(TsProcessor& p);
    ~CdDrive();  // stops the reader before the engine (and its port) goes

    // An .iso or a .cue (A127) into the drive; on failure (err) the drive keeps what it had.
    bool insert(const juce::File& f, juce::String& err);
    bool eject();          // false when the drive is busy
    juce::String name() const { return file_.getFileName(); }  // the file name, empty when none
    // TsProcessor::tick: hands the sectors served since the last poll to the record (Project mode only, A121).
    void poll();
    // After every restart: the engine whose port the reader answers (null with none).
    void setEngine(ens::app::Engine* e);
    // A stand-in (Task 9): asks for `medium` are answered from m's recorded sectors, a sector it lacks with failure.
    // nullptr stops serving it; call that before m is freed or changed.
    void serveFrom(const ens::app::SavedMedium* m, uint32_t medium);
    // A fresh medium id for serveFrom: the drive's ids count up from 1 and are never reused (T29).
    uint32_t newMedium() { return next_++; }

    // The dialogs, message thread: the chooser, and the error of an insert or eject asked from the menu or a drop.
    void chooseInsert();
    void insertAsked(const juce::File& f);
    void ejectAsked();

private:
    struct Source {
        juce::File file;       // the image file; none for a stand-in
        ens::ts::CdLayout layout;
        uint32_t sectors = 0;
        const ens::app::SavedMedium* saved = nullptr;  // a stand-in's sectors
    };
    struct Served {
        uint32_t size, lba;
        std::vector<uint8_t> data;
    };
    class Reader;
    bool serveOne();  // the reader thread: answers the ask pending, if any
    juce::String notNow() const;

    TsProcessor& proc_;
    uint32_t next_ = 1;  // the next medium id
    juce::File file_;  // the image in the drive (the .iso or .cue chosen); none when empty
    std::unique_ptr<juce::FileChooser> chooser_;
    // DECISION A128: the reader thread and the message thread share these under mu_ (neither is the audio thread).
    std::mutex mu_;
    ens::ts::SectorPort* port_ = nullptr;
    uint32_t portGen_ = 0;               // counts setEngine calls: a reply is sent only to the port it was read for
    std::map<uint32_t, Source> sources_;  // by medium id
    std::vector<Served> served_;          // for poll()
    std::unique_ptr<Reader> reader_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(CdDrive)
    JUCE_DECLARE_NON_COPYABLE(CdDrive)
};
