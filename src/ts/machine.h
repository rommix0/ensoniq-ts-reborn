#pragma once
// The TS-10 as a machine: the 68000, the DUART and the OTTO/ESP board joined by the address map,
// run slot by slot against the OTTO voice scan. Pure emulation: no file I/O (the caller reads the
// ROM files). Integer-only; state is bit-identical across builds. Pattern: KS-32 Reborn's Machine.
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "duart/duart.h"
#include "m68k/m68k.h"
#include "ts/board.h"
#include "ts/cd_target.h"
#include "ts/fdc.h"
#include "ts/panel.h"
#include "ts/roms.h"
#include "ts/sector_port.h"
#include "ts/wd33c93.h"

namespace ens::ts {

enum class Device : uint8_t { Otto, Esp, Duart, Fdc, Iexp, Scsi, Unmapped, Iack };

struct BusEvent {
    uint64_t cycle;  // cpu.totalCycles() when the access is made
    uint32_t addr;   // 24-bit address; Iack: the level
    uint16_t value;  // value read or written; Iack: the vector answered
    uint8_t size;    // 1 or 2
    bool write;
    Device dev;
};

// The analog mux inputs (U69 Y0-Y7); resting values in kAnalogRest (T8). Pressure is Y1: the TS-12's keyboard
// pressure, not connected on the TS-10 (T31).
enum class Analog : uint8_t { PitchWheel, ModWheel, Volume, CvPedal, DataEntry, Battery, Reference, Pressure };
constexpr int kAnalogCount = 8;
// DECISION T8: the raw 10-bit value each input holds from power-on, in Analog order (docs/ts-hardware-interface.md,
// "Analog inputs"). Pressure rests at 0, no pressure.
constexpr uint16_t kAnalogRest[kAnalogCount] = {0x200, 0x000, 0x3FF, 0x3FF, 0x14E, 0x000, 0x324, 0x000};

constexpr uint32_t kCpuHz = 15'238'090, kDuartHz = 4'000'000;  // DECISION T1
constexpr uint32_t kBootMs = 5'300;                            // firmware tests: boot end (3491 ms) + 50 %
constexpr int kSlotClocks = 16;                                // OTTO: 16 CLKs per voice (notes §4)
constexpr int kPanelChannel = 1;                               // DECISION T7: DUART B is the display/keyboard link
constexpr int kMidiChannel = 0;                                // DECISION T7: DUART A is MIDI

// Threading: every call on one Machine comes from one thread at a time; the engine owns it on the audio
// thread after the boot. Nothing in runFrame or the input calls allocates (tests/rt_main.cpp).
class Machine : public m68k::Bus {
public:
    // nullptr on a wrong ROM size, with err naming the file. DECISION T13: a stock TS-10 (design section 4)
    // unless sqx70: the SQX-70 sequence expander's 256 KB (SEQEXP) fitted. DECISION T25: scsi fits the SP-4 board
    // (a WD33C93 at I/O1 n = 1) with a CD-ROM drive at SCSI ID 4; without it the block reads 0 (T12).
    // DECISION T4: simm8 fits the 4M x 8 SIMM pair (8 MB, BankSets S8 and S9) in place of the factory 2 MB.
    // DECISION T31: model sets the keyboard (keyRange, no Poly-Key pressure on the TS-12); roms holds that model's OS.
    static std::unique_ptr<Machine> create(const RomSet& roms, std::string& err, bool sqx70 = false,
                                           bool scsi = false, bool simm8 = false, Model model = Model::Ts10);
    Model model() const { return model_; }
    Machine(const Machine&) = delete;  // cpu_ holds a Bus& to *this
    Machine& operator=(const Machine&) = delete;
    void runFrame(Audio& out);                              // one OTTO frame (ACTV + 1 slots)
    uint64_t cycles() const { return cpu_.totalCycles(); }  // CPU clocks since the machine was created (T1)
    uint16_t peek16(uint32_t addr) const;                   // memory only (devices read 0), no side effects
    // DECISION T13: the battery RAM, raw: OSRAM (256 KB) then SEQEXP (256 KB), each in CPU address order;
    // the same layout with or without the SQX-70 (the SEQEXP half is unused on a stock TS).
    const std::vector<uint8_t>& ramImage() const { return ram_; }
    // A power cycle with the battery holding the RAM: the image (exactly ramImage().size() bytes) replaces
    // the RAM, the sample DRAM and the ESP delay DRAM are lost, the CPU and every device restart; the analog
    // and IEXP inputs keep their values. A wrong size fails with err set and changes nothing.
    bool loadRamImage(const std::vector<uint8_t>& img, std::string& err);
    std::function<void(const BusEvent&)> onBusEvent;        // the bus trace; off when empty
    std::function<void(uint8_t)> onPanelByte;               // each byte the panel receives on TxDB; off when empty
    const PanelState& panel() const { return panel_.state(); }
    // Panel and keyboard input (T16, T17): queued in the keyboard and sent on RxDB from the next slot.
    void button(Button b, bool down) { panel_.button(b, down); }
    void key(int note, bool down, int velocity) { panel_.key(note, down, velocity); }
    void pressure(int note, int amount) { panel_.pressure(note, amount); }
    // Bytes the panel's ring takes now without dropping one (A2). The slot hands that ring to DUART B
    // only while its wire queue is empty, so the ring is the only limit: a caller that queues a button
    // or key (2 bytes) only when this is 2 or more never loses one (A102).
    int panelRoom() const { return static_cast<int>(Panel::kToCpuSize - panel_.toCpuQueued()); }
    uint32_t panelDropped() const { return panel_.dropped(); }  // events lost to a full ring since the last power-on
    // The Seqs/Songs LED: DUART OP3 (SEQLED/), active low (docs/ts-hardware-interface.md, "Display and LEDs").
    bool seqLed() const { return (duart_.outputPins() & 0x08) == 0; }
    // DECISION T20: a byte on MIDI in joins DUART A's wire queue (up to 1024, A1) and enters at the line
    // rate, 320 us a byte at 31 250 baud (D2), so a host burst cannot overrun the receiver.
    void midiIn(uint8_t byte) { duart_.receive(kMidiChannel, byte); }
    // The oldest byte the firmware sent on MIDI out and not yet popped (256 are kept, A2).
    bool popMidiOut(uint8_t& b) { return duart_.popTransmitted(kMidiChannel, b); }
    void setAnalog(Analog a, uint16_t raw10);  // a raw 10-bit conversion, clamped to 0-1023 (T8)
    uint16_t analog(Analog a) const;           // the value last set, kAnalogRest until then
    // IEXP inputs (T9): n = 3 or 4 (Foot Switch 2's contacts; 1 and 2 go through the keyboard as buttons),
    // side 0 = left, 1 = right patch select button. Read by the OS on its next IEXP poll.
    void footswitch(int n, bool down);
    void patchSelect(int side, bool down);
    // The floppy drive (T23): the disk goes in at once (between frames), the one taken out is returned (nullptr
    // when the drive was empty); nullptr ejects. Never allocates. The OS sees a disk change on IP1.
    std::unique_ptr<Disk> insertDisk(std::unique_ptr<Disk> d);
    const Disk* disk() const { return disk_.get(); }
    Disk* disk() { return disk_.get(); }
    // The CD-ROM drive at SCSI ID 4 (T25, T28, T29), only with the SP-4 board: nullptr without it. Its sectors come
    // through cdPort(), answered by whoever owns the image (T29). insertCd applies at once (between frames) and never
    // allocates: sectors 0 ejects, any change gives the OS one UNIT ATTENTION. The medium id names the image in the
    // port's asks and replies, so a late reply for an earlier disc is never served: never reuse an id for a different
    // image (0 means none). Does nothing without the board.
    bool scsiFitted() const { return scsi_; }
    SectorPort* cdPort() { return scsi_ ? &cdPort_ : nullptr; }
    void insertCd(uint32_t medium, uint32_t sectors) {
        if (scsi_) cd_.insert(medium, sectors);
    }
    const CdTarget* cd() const { return scsi_ ? &cd_ : nullptr; }
    m68k::Cpu& cpu() { return cpu_; }
    Board& board() { return board_; }
    duart::Duart& duart() { return duart_; }
    // DECISION T5: interrupt level (1-3, 0 = not connected) of each source: OTTO 1, DUART 3 (the disk
    // line, level 2, has no source). Settable for experiments.
    void setIrqLevels(int otto, int duart) {
        ottoLevel_ = otto;
        duartLevel_ = duart;
    }

    // m68k::Bus
    uint8_t read8(uint32_t addr) override;
    uint16_t read16(uint32_t addr) override;
    void write8(uint32_t addr, uint8_t v) override;
    void write16(uint32_t addr, uint16_t v) override;
    int interruptAcknowledge(int level) override;
    void resetPeripherals() override;

private:
    Machine() = default;
    void powerOn();
    enum class Region : uint8_t { None, Rom, Ram, Host, Duart, Scsi, Fdc, Iexp, Otto, Esp };
    static Region decode(uint32_t a);
    static Device deviceOf(Region r);
    uint8_t romByte(uint32_t off) const;
    uint32_t ramIndex(uint32_t a) const;  // a RAM address's byte in ram_; kNoRam: an empty SEQEXP socket
    static constexpr uint32_t kNoRam = UINT32_MAX;
    uint16_t hostWord(uint32_t a) const;
    uint8_t memByte(uint32_t a, bool& mapped) const;  // ROM, RAM and the sample-RAM window
    uint8_t ioRead(Region r, uint32_t a);
    void ioWrite(Region r, uint32_t a, uint8_t v);
    void trace(uint32_t addr, uint16_t value, uint8_t size, bool write, Device dev);
    void updateLines();  // IPL, the ESP's HALT pin, the OTTO pot
    void slot(int i, int n);
    uint8_t drivePins() const;  // DUART IP5..IP0 with a disk in (T23, T24)

    WaveMemory waves_;  // before board_: the OTTO holds a reference
    Board board_{waves_};
    duart::Duart duart_{kDuartHz};
    Fdc fdc_;
    std::unique_ptr<Disk> disk_;  // the disk in the drive (fdc_ holds a plain pointer to it)
    uint64_t indexEdge_ = 0;      // cycles() of the next index-pulse edge while a disk is in (T23)
    bool indexLow_ = false;       // IP0 (DSTAT) low: the index hole is passing
    bool dskchg_ = true;          // IP1 (DSKCHG) high: a disk went in or out and the head has not stepped since
    int stepCyl_ = 0;             // the head's cylinder when the disk changed
    bool scsi_ = false;           // the SP-4 board fitted (T25)
    SectorPort cdPort_;
    CdTarget cd_{cdPort_};        // SCSI ID 4 (T28)
    Wd33c93 wd_;
    Panel panel_;
    m68k::Cpu cpu_{*this};
    std::vector<uint16_t> os_;  // 256K words, T2
    static constexpr uint32_t kOsRamBytes = 0x40000;              // DECISION T13: OSRAM (U3/U4) 256 KB
    static constexpr uint32_t kRamBytes = 2 * kOsRamBytes;        // then SEQEXP (U1/U2, the SQX-70) 256 KB
    std::vector<uint8_t> ram_ = std::vector<uint8_t>(kRamBytes, 0);
    bool overlay_ = true;  // reads of 0-7 come from the ROM until the first write there
    bool sqx70_ = false;   // SEQEXP fitted (T13)
    Model model_ = Model::Ts10;
    std::array<uint16_t, 8> mux_{};  // the POT multiplexer's inputs by select (DUART OP2..OP0), T8
    uint8_t iexpDown_ = 0;           // IEXP D0-D3 held low (pressed), T9
    int ottoLevel_ = 1, duartLevel_ = 3;
    int carry_ = 0;  // CPU cycles run past the end of the last slot
    uint32_t duartAcc_ = 0;
};

}  // namespace ens::ts
