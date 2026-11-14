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
#include "ts/fdc.h"
#include "ts/panel.h"
#include "ts/roms.h"

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

// The analog mux inputs (U69 Y0-Y7 without the unconnected Y1); resting values in kAnalogRest (T8).
enum class Analog : uint8_t { PitchWheel, ModWheel, Volume, CvPedal, DataEntry, Battery, Reference };
constexpr int kAnalogCount = 7;
// DECISION T8: the raw 10-bit value each input holds from power-on, in Analog order (docs/ts-hardware-interface.md,
// "Analog inputs").
constexpr uint16_t kAnalogRest[kAnalogCount] = {0x200, 0x000, 0x3FF, 0x3FF, 0x14E, 0x000, 0x324};

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
    // unless sqx70: the SQX-70 sequence expander's 256 KB (SEQEXP) fitted.
    static std::unique_ptr<Machine> create(const RomSet& roms, std::string& err, bool sqx70 = false);
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
    // DECISION T20: a byte on MIDI in joins DUART A's wire queue (up to 1024, A1) and enters at the line
    // rate, 320 us a byte at 31 250 baud (D2), so a host burst cannot overrun the receiver.
    void midiIn(uint8_t byte) { duart_.receive(kMidiChannel, byte); }
    // The oldest byte the firmware sent on MIDI out and not yet popped (256 are kept, A2).
    bool popMidiOut(uint8_t& b) { return duart_.popTransmitted(kMidiChannel, b); }
    void setAnalog(Analog a, uint16_t raw10);  // a raw 10-bit conversion, clamped to 0-1023 (T8)
    // IEXP inputs (T9): n = 3 or 4 (Foot Switch 2's contacts; 1 and 2 go through the keyboard as buttons),
    // side 0 = left, 1 = right patch select button. Read by the OS on its next IEXP poll.
    void footswitch(int n, bool down);
    void patchSelect(int side, bool down);
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

    WaveMemory waves_;  // before board_: the OTTO holds a reference
    Board board_{waves_};
    duart::Duart duart_{kDuartHz};
    Fdc fdc_;
    Panel panel_;
    m68k::Cpu cpu_{*this};
    std::vector<uint16_t> os_;  // 256K words, T2
    static constexpr uint32_t kOsRamBytes = 0x40000;              // DECISION T13: OSRAM (U3/U4) 256 KB
    static constexpr uint32_t kRamBytes = 2 * kOsRamBytes;        // then SEQEXP (U1/U2, the SQX-70) 256 KB
    std::vector<uint8_t> ram_ = std::vector<uint8_t>(kRamBytes, 0);
    bool overlay_ = true;  // reads of 0-7 come from the ROM until the first write there
    bool sqx70_ = false;   // SEQEXP fitted (T13)
    std::array<uint16_t, 8> mux_{};  // the POT multiplexer's inputs by select (DUART OP2..OP0), T8
    uint8_t iexpDown_ = 0;           // IEXP D0-D3 held low (pressed), T9
    int ottoLevel_ = 1, duartLevel_ = 3;
    int carry_ = 0;  // CPU cycles run past the end of the last slot
    uint32_t duartAcc_ = 0;
};

}  // namespace ens::ts
