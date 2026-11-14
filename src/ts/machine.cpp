#include "ts/machine.h"

#include <algorithm>

namespace ens::ts {

namespace {
constexpr uint32_t kMask24 = 0xFFFFFF;
constexpr uint32_t kRomBase = 0xC00000, kRomWindow = 0x100000, kRomBytes = 0x80000;
constexpr uint32_t kRamBase = 0xFF0000;
}  // namespace

std::unique_ptr<Machine> Machine::create(const RomSet& roms, std::string& err) {
    std::unique_ptr<Machine> m(new Machine());
    if (!buildOsImage(roms, m->os_, err) || !m->waves_.load(roms, err)) return nullptr;
    // DECISION T1: every KS-32 clock rate carries over: 4 MHz X1, the serial clock 500 kHz on IP3/IP4.
    m->duart_.setExternalClock(3, 500'000);
    m->duart_.setExternalClock(4, 500'000);
    m->duart_.setInputPins(0x3F);
    m->powerOn();
    return m;
}

void Machine::powerOn() {
    board_.otto().reset();
    board_.esp().reset();
    duart_.reset();
    carry_ = 0;
    duartAcc_ = 0;
    cpu_.reset();  // the 40 reset cycles count in cycles() and are not slot carry
    updateLines();
}

// Initial map (Task 3): the 512 KB OS image at 0xC00000, mirrored over 0xC00000-0xCFFFFF, and at
// 0 for the reset fetch (provisional, Task 4 finds the real overlay); 64 KB RAM at 0xFF0000.
uint8_t Machine::memByte(uint32_t a, bool& mapped) const {
    mapped = true;
    const bool rom = (a >= kRomBase && a < kRomBase + kRomWindow) || a < kRomBytes;
    if (rom) {
        const uint16_t w = os_[(a & (kRomBytes - 1)) >> 1];
        return static_cast<uint8_t>((a & 1) ? w : w >> 8);
    }
    if (a >= kRamBase) return ram_[a - kRamBase];
    mapped = false;
    return 0;
}

uint16_t Machine::peek16(uint32_t addr) const {
    bool mapped;
    addr &= kMask24 & ~1u;
    const uint8_t hi = memByte(addr, mapped);
    const uint8_t lo = memByte(addr + 1, mapped);
    return static_cast<uint16_t>((hi << 8) | lo);
}

void Machine::trace(uint32_t addr, uint16_t value, uint8_t size, bool write, Device dev) {
    if (onBusEvent) onBusEvent({cpu_.totalCycles(), addr, value, size, write, dev});
}

// DECISION T5: the IPL is the highest level among the asserting devices (levels 1-3, all autovectored).
// DECISION T7: DUART OP6 is ESPHALT, active high (the pin is high while OPR bit 6 is 0); OP2..OP0 select
// the analog mux input on the OTTO POT pin, the conversion being immediate.
void Machine::updateLines() {
    int level = 0;
    if (ottoLevel_ && board_.otto().irqAsserted()) level = std::max(level, ottoLevel_);
    if (duartLevel_ && duart_.irq()) level = std::max(level, duartLevel_);
    cpu_.setIpl(level);
    const uint8_t pins = duart_.outputPins();
    board_.esp().setHaltPin((pins & 0x40) != 0);
    board_.otto().setPot(mux_[pins & 7]);
}

int Machine::interruptAcknowledge(int level) {
    trace(static_cast<uint32_t>(level), static_cast<uint16_t>(24 + level), 1, false, Device::Iack);
    return m68k::kAutovector;  // DECISION T5: AVEC from PAL U67 for every interrupt
}

// DECISION T14: the RESET instruction drives BRES/, which resets the OTTO; the DUART and FDC are on
// the power-on-only BRES and are untouched.
void Machine::resetPeripherals() {
    board_.otto().reset();
    updateLines();
}

// DECISION T12: everything but ROM and RAM is unmapped for now: reads 0, writes ignored, all traced.
uint8_t Machine::read8(uint32_t a) {
    a &= kMask24;
    bool mapped;
    const uint8_t m = memByte(a, mapped);
    if (mapped) return m;
    trace(a, 0, 1, false, Device::Unmapped);
    return 0;
}

uint16_t Machine::read16(uint32_t a) {
    a &= kMask24;
    bool mapped;
    const uint8_t hi = memByte(a, mapped);
    if (mapped) return static_cast<uint16_t>((hi << 8) | memByte(a + 1, mapped));
    trace(a, 0, 2, false, Device::Unmapped);
    return 0;
}

void Machine::write8(uint32_t a, uint8_t v) {
    a &= kMask24;
    bool mapped;
    memByte(a, mapped);
    if (!mapped) {
        trace(a, v, 1, true, Device::Unmapped);
        return;
    }
    if (a >= kRamBase) ram_[a - kRamBase] = v;  // ROM writes are ignored
}

void Machine::write16(uint32_t a, uint16_t v) {
    a &= kMask24;
    bool mapped;
    memByte(a, mapped);
    if (!mapped) {
        trace(a, v, 2, true, Device::Unmapped);
        return;
    }
    if (a >= kRamBase) {
        ram_[a - kRamBase] = static_cast<uint8_t>(v >> 8);
        ram_[a - kRamBase + 1] = static_cast<uint8_t>(v);
    }
}

// DECISION T1: one voice slot = 16 CPU clocks, with the overshoot of the last instruction carried into
// the next slot (a carry of 16 or more skips the CPU); then the OTTO slot, the DUART by an exact
// rational X1 accumulator, the ESP idle steps while halted, and the lines. Slot i of an n-slot frame.
void Machine::slot(int i, int n) {
    if (carry_ >= kSlotClocks) {
        carry_ -= kSlotClocks;
    } else {
        const int want = kSlotClocks - carry_;
        carry_ = cpu_.run(want) - want;
    }
    board_.otto().stepVoice();
    duartAcc_ += static_cast<uint32_t>(kSlotClocks) * kDuartHz;
    const uint32_t x1 = duartAcc_ / kCpuHz;
    duartAcc_ %= kCpuHz;
    if (x1) duart_.advance(x1);
    // DECISION T11: while the ESP is halted slot i gets its share of the frame's idle steps.
    Esp& esp = board_.esp();
    if (esp.halted()) {
        const int a = std::min(i, n), b = std::min(i + 1, n);
        const int idle = kEspSlotsPerFrame * b / n - kEspSlotsPerFrame * a / n;
        for (int k = 0; k < idle; ++k) esp.idleStep();
    }
    updateLines();
}

void Machine::runFrame(Audio& out) {
    Otto& otto = board_.otto();
    const int n = otto.activeVoices();  // a CPU write to ACTV mid-frame only moves the end
    int i = 0;
    do {
        slot(i++, n);
    } while (!otto.frameReady());
    out = board_.route(otto.takeFrame(), board_.esp().halted() ? 0 : kEspSlotsPerFrame);
}

}  // namespace ens::ts
