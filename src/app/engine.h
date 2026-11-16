#pragma once
// The engine between the TS machine and a host's audio callback (Plan 4, from KS-32 Reborn's): it renders
// Main and Aux at the host rate through the resampler, takes MIDI with block offsets (A101) and GUI
// events through a fixed queue (A102), and publishes a panel snapshot (A103, A104). JUCE-free and
// OS-free: no file I/O.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/resampler.h"
#include "app/spsc_ring.h"
#include "ts/machine.h"

namespace ens::app {

// DECISION A100: the machine's frame rate, the engine's input rate: one OTTO frame of 32 voices is
// 512 CPU clocks (T1), 29 761.9 Hz. Here, not in src/ts, which stays integer-only.
constexpr double kFrameHz = ts::kCpuHz / 512.0;

struct Event {
    enum class Kind : uint8_t { Button, Key, Analog, PatchSelect };
    Kind kind;
    // Button: ts::Button code. Key: note 36-96. Analog: ts::Analog. PatchSelect: 0 = left, 1 = right.
    uint8_t id;
    bool down;
    uint16_t value;  // Key: strike 1-127. Analog: raw.
};

struct Snapshot {
    ts::PanelState panel{};
    bool seqLed = false;  // the Seqs/Songs LED: DUART OP3 low (interface doc, "Display and LEDs")
    bool booted = false;
    uint64_t frames = 0;        // machine frames since the last power-on (create or loadRam)
    uint32_t midiDropped = 0;   // MIDI-in bytes dropped: the block store (A101) plus the DUART (A1)
    uint64_t midiOutBytes = 0;  // MIDI-out bytes drained and discarded (A2)
    int16_t program = -1;       // the current sound 0-127; -1 until the boot ends (A105)
    std::array<uint8_t, 8> select{};  // the bytes that select it: bank select, then Cn pp (A105)
    uint8_t selectLen = 0;            // 0 until a selection has been seen or sent
    std::array<uint64_t, 2> notes{};  // MIDI-in note mask, bit n is note n (A107)
    uint32_t programsSent = 0;  // selections the engine has sent into MIDI in since create (A105)
    uint32_t programSeq = 0;    // program changes tracked from MIDI out or in, not the engine's own sends
    bool diskIn = false;        // a disk is in the drive (A118)
    uint32_t diskWrites = 0;    // Disk::writes() of the disk in the drive
    uint32_t diskSwaps = 0;     // inserts and ejects applied, so a copy can be matched to its disk
    uint32_t diskReads = 0;     // Disk::reads() of the disk in the drive
    uint32_t cdMedium = 0;      // the medium id of the CD in the drive; 0 none (applied, not pending)
    bool restoring = false;     // the auto-load is restoring from stand-ins (A131); booted waits for its end
};

class Engine {
public:
    ~Engine();
    // scsi: the SP-4 board with a CD drive at ID 4 (Plan 7).
    static std::unique_ptr<Engine> create(const ts::RomSet&, std::string& err, bool scsi = false);
    // Message thread, audio stopped:
    void prepare(double outRate, int maxBlock);  // allocates the resampler and buffers
    bool loadRam(const std::vector<uint8_t>& img, std::string& err);
    // The exact bytes to replay after this boot (A105), when they select the program setProgram asks
    // for, instead of a bare program change, and at the boot's end even when the machine is already on
    // that program number (R2-0 against the boot's U0-0). Call after loadRam; up to 8 bytes.
    void setSelect(const uint8_t* bytes, int len);
    // DECISION A124: answer the OS's power-on AUTO-LOAD ALL SAMPLED-SOUNDS question with *NO * (the VST3's
    // engines). Before the audio starts; kept across loadRam.
    void setAnswerAutoLoad(bool on);
    // DECISION A130, A131: stand-in media built from what the OS read when the sounds were loaded (media_record.h).
    // Labels are as ensoniqLabel gives them, one per floppy and per CD; CD medium ids are answered on cdPort().
    struct StandIns {
        std::vector<std::unique_ptr<ts::Disk>> floppies;
        std::vector<std::string> floppyLabels;
        std::vector<uint32_t> cdMedia, cdSizes;
        std::vector<std::string> cdLabels;
    };
    // Message thread, after loadRam, before the audio starts, with the drive empty (a new engine): the first floppy
    // and the first CD go in at once, the power-on question gets *YES*, each insert prompt the stand-in it names and
    // *OK* (*SKIP* when none matches), and the A118/A119/A128 slots wait until the auto-load ends (so a disk or CD
    // queued now goes in after it). Empty stand-ins, or a disk in the drive: nothing changes (A124 as today).
    void setRestore(StandIns s);
    // Message thread, once snapshot().restoring is false: the floppy stand-ins (the drive is left empty and the CD
    // ejected), with the blocks the auto-load read marked; empty while restoring.
    std::vector<std::unique_ptr<ts::Disk>> takeStandIns();
    ts::Machine& machine();  // tests, and the RAM at shut-down
    // Message thread, audio running:
    bool post(const Event&);    // false when the queue is full (A102)
    Snapshot snapshot() const;  // A103
    void requestRam();          // the next block copies the RAM
    bool takeRam(std::vector<uint8_t>& out);  // true once a requested copy is ready
    // DECISION A118: disks cross threads in single-slot atomics, applied at the next block start; the audio
    // thread never allocates or frees one. The caller allocates a scratch disk and frees what comes back.
    bool insertDisk(std::unique_ptr<ts::Disk> d);  // nullptr ejects; false while a swap is pending
    std::unique_ptr<ts::Disk> takeRemoved();       // the disk the last swap took out; nullptr when none
    bool requestDisk(std::unique_ptr<ts::Disk> scratch);  // a disk of the current density; false when pending
    // true once done; ok false: no disk or other density. swapsAtCopy is the swap count when the copy was
    // made: an app counting its own accepted inserts and ejects matches the copy to its disk by it.
    bool takeDisk(std::unique_ptr<ts::Disk>& out, bool& ok, uint32_t& swapsAtCopy);
    // DECISION A119: the write protect of the disk in the drive, set at the next block start (after a swap in
    // that block); a request with the drive empty is dropped, so the caller also sets it on a disk it inserts.
    void setWriteProtect(bool p);
    // DECISION A119: message thread, audio stopped (a restart): the disk in the drive, out of it, with a
    // pending insert or eject applied first. The rest (a disk replaced by that insert, the slots) goes with
    // the engine.
    std::unique_ptr<ts::Disk> releaseDisk();
    // DECISION A128: the CD drive's single slot, as the disk's: applied at the next block start, false while one is
    // pending, 0 sectors ejects. cdPort() is stable for the engine's life (nullptr without SCSI); another thread
    // answers its asks (T29) and never reuses a medium id for a different image.
    ts::SectorPort* cdPort();
    bool insertCd(uint32_t medium, uint32_t sectors);
    // Message thread, audio stopped (a restart): the CD in the drive as {medium, sectors}, or the pending insert
    // taken back unapplied; {0, 0} when none.
    std::pair<uint32_t, uint32_t> releaseCd();
    // Usage: after every swap that took a disk out, call takeRemoved (until it returns it) before the next
    // swap can apply; an insert into an empty drive parks nothing. takeDisk's `out` is the scratch, also
    // when ok is false, and is freed by the caller. The engine's destructor frees what is left in a slot.
    // Audio thread:
    void midi(int offset, const uint8_t* data, int size);  // before render, offsets in this block (A101)
    // Any frames >= 0, in pieces of maxBlock.
    void render(float* mainL, float* mainR, float* auxL, float* auxR, int frames);
    void setProgram(int n);    // requests a sound 0-127 (A105)
    void adoptProgram(int n);  // as setProgram, but never sends: a value followed from the snapshot
    void setAnalog(ts::Analog a, uint16_t raw);  // applied at a block start, once booted (A106)

private:
    explicit Engine(std::unique_ptr<ts::Machine> m);
    void apply(const Event& e);
    void renderPiece(float* const out[4], int frames);
    void feedMidi(uint64_t upTo);  // the stored bytes due before block frame upTo
    void blockStart();             // sends the requested sound, applies the analogs (A105, A106)
    void answerAutoLoad();         // A124: presses and releases *NO * under the power-on question (A130: *YES*)
    void restoreStep();            // A131: answers the insert prompts, ends the restore
    void putFloppy(int i);         // A131: stand-in i into the drive, the one in it back to its slot
    void endRestore();
    void sendSelect(const uint8_t* b, int n);

    struct MidiByte {
        uint64_t frame;  // machine frame of the block it enters before (A101)
        uint8_t byte;
    };
    // A running-status MIDI parser for one stream: feed returns true when a message is complete.
    struct MidiParse {
        uint8_t status = 0, d[2] = {0, 0};
        int n = 0;
        bool sysex = false;
        bool feed(uint8_t b);
    };
    void track(const MidiParse& p, bool fromIn);  // a complete message: program, bank, notes

    static constexpr int kMidiStore = 1024;     // DECISION A101
    static constexpr size_t kEventQueue = 256;  // DECISION A102
    static constexpr size_t kChannels = 4;      // Main L, Main R, Aux L, Aux R

    std::unique_ptr<ts::Machine> m_;
    std::unique_ptr<Resampler> rs_;
    std::vector<float> out_;  // resampled frames not yet handed out, interleaved Main L R, Aux L R
    double outRate_ = 48000;
    int maxBlock_ = 0;
    uint64_t frames_ = 0;      // machine frames since power-on
    uint64_t blockFrame_ = 0;  // machine frames run in this block
    MidiByte midi_[kMidiStore];
    int midiCount_ = 0, midiNext_ = 0;
    uint32_t storeDropped_ = 0;
    uint64_t midiOut_ = 0;
    SpscRing<Event, kEventQueue> events_;
    Event held_{};  // popped, waiting for room on the panel link
    bool holding_ = false;
    mutable std::mutex snapMu_;
    Snapshot snap_;
    std::vector<uint8_t> ram_;
    // The sound (A105), reset by loadRam as a power cycle.
    MidiParse inParse_, outParse_;
    bool booted_ = false;  // the tracker's boot flag: frames_ reached kBootFrames
    // A124: Off, or watching this boot for the question, *NO * held since autoLoadAt_, or done.
    enum class AutoLoad : uint8_t { Off, Watch, Held, Done };
    bool answerAutoLoad_ = false;
    AutoLoad autoLoad_ = AutoLoad::Off;
    uint64_t autoLoadAt_ = 0;
    ts::Button autoLoadButton_ = ts::Button::Soft6;
    // A131: the stand-ins (audio thread while restoring_, then takeStandIns), the one in the drive, the prompt press.
    StandIns restore_;
    std::atomic<bool> restoring_{false};
    int floppyIn_ = -1;
    bool promptArmed_ = true, promptHeld_ = false;
    ts::Button promptButton_ = ts::Button::Soft6;
    uint64_t promptAt_ = 0;
    int requested_ = -1, current_ = -1;
    bool dirty_ = false, sending_ = false;
    uint32_t programSeq_ = 0;
    uint8_t channel_ = 0;  // the channel last seen in a program change
    bool bankSeen_ = false;
    uint8_t bankMsb_ = 0, bankLsb_ = 0;
    std::array<uint8_t, 8> select_{};
    uint8_t selectLen_ = 0;
    std::array<uint8_t, 8> setSel_{};
    uint8_t setSelLen_ = 0;
    int setSelProgram_ = -1;
    uint32_t programsSent_ = 0;
    std::array<uint64_t, 2> notes_{};                // A107
    std::array<uint16_t, ts::kAnalogCount> wantAnalog_{};  // A106
    std::array<bool, ts::kAnalogCount> wantSet_{};
    std::atomic<bool> ramWanted_{false}, ramReady_{false};
    // A118. swapWanted_ publishes swapIn_ (message to audio) and clears when applied; removedFull_ publishes
    // removed_ (audio to message); copyState_ 0 idle, 1 requested (scratch_ set), 2 done (copyOk_ set).
    std::atomic<bool> swapWanted_{false}, removedFull_{false};
    std::atomic<ts::Disk*> swapIn_{nullptr}, removed_{nullptr}, scratch_{nullptr};
    std::atomic<int> copyState_{0};
    std::atomic<bool> copyOk_{false};
    std::atomic<uint32_t> copySwaps_{0};
    std::atomic<int> protect_{-1};  // A119: -1 none, else the write protect asked for
    uint32_t diskSwaps_ = 0;
    // A128: cdWanted_ publishes cdIn_ (medium << 32 | sectors); the audio thread clears it when applied.
    std::atomic<bool> cdWanted_{false};
    std::atomic<uint64_t> cdIn_{0};
};

}  // namespace ens::app
