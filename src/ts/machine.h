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

constexpr uint32_t kCpuHz = 15'238'090, kDuartHz = 4'000'000;  // DECISION T1
constexpr int kSlotClocks = 16;                                // OTTO: 16 CLKs per voice (notes §4)
constexpr int kPanelChannel = 1;                               // DECISION T7: DUART B is the display/keyboard link
constexpr int kMidiChannel = 0;                                // DECISION T7: DUART A is MIDI

class Machine : public m68k::Bus {
public:
    // nullptr on a wrong ROM size, with err naming the file.
    static std::unique_ptr<Machine> create(const RomSet& roms, std::string& err);
    Machine(const Machine&) = delete;  // cpu_ holds a Bus& to *this
    Machine& operator=(const Machine&) = delete;
    void runFrame(Audio& out);                              // one OTTO frame (ACTV + 1 slots)
    // Emulated CPU time since the machine was created, in clocks: what the CPU ran minus the overshoot of
    // its last instruction, which the next slot repays (so a long DIVU at a frame's end does not show).
    uint64_t cycles() const { return cpu_.totalCycles() - static_cast<uint64_t>(carry_); }
    uint16_t peek16(uint32_t addr) const;                   // ROM and RAM only, devices read 0, no side effects
    std::function<void(const BusEvent&)> onBusEvent;        // the bus trace; off when empty
    m68k::Cpu& cpu() { return cpu_; }
    Board& board() { return board_; }
    duart::Duart& duart() { return duart_; }
    // DECISION T5: interrupt level (1-3, 0 = not connected) of each source; Task 4 finds the real ones.
    // The default is a placeholder: DUART 3, OTTO 2, disk 1 (the disk line has no source).
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
    uint8_t memByte(uint32_t a, bool& mapped) const;  // ROM and RAM
    void trace(uint32_t addr, uint16_t value, uint8_t size, bool write, Device dev);
    void updateLines();  // IPL, the ESP's HALT pin, the OTTO pot
    void slot(int i, int n);

    WaveMemory waves_;  // before board_: the OTTO holds a reference
    Board board_{waves_};
    duart::Duart duart_{kDuartHz};
    m68k::Cpu cpu_{*this};
    std::vector<uint16_t> os_;  // 256K words, T2
    // DECISION T13 (provisional until Task 4): 64 KB RAM at 0xFF0000.
    std::vector<uint8_t> ram_ = std::vector<uint8_t>(0x10000, 0);
    // The POT multiplexer's inputs by select (DUART OP2..OP0); resting values come in Task 6.
    std::array<uint16_t, 8> mux_{};
    int ottoLevel_ = 2, duartLevel_ = 3;
    int carry_ = 0;  // CPU cycles run past the end of the last slot
    uint32_t duartAcc_ = 0;
};

}  // namespace ens::ts
