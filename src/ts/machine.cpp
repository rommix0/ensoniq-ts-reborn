#include "ts/machine.h"

#include <algorithm>

namespace ens::ts {

namespace {
constexpr uint32_t kMask24 = 0xFFFFFF;
// DECISION T13: the map found by running the OS (docs/ts-hardware-interface.md, "Memory map").
constexpr uint32_t kRomBase = 0xC00000, kRomWindow = 0x100000, kRomBytes = 0x80000;  // mirror unobserved
constexpr uint32_t kRamLowTop = 0x040000, kRamHighBase = 0xFC0000;  // OSRAM at 0 and again at the top
constexpr uint32_t kIo1Base = 0x200000, kIo1Window = 0x100000;      // A19:A18 = DUART, SCSI, FDC, IEXP
constexpr uint32_t kIo2Base = 0x300000, kIo2Window = 0x100000;      // A19 = OTTO, ESP
constexpr uint32_t kHostBase = 0x400000, kHostWindow = 0x800000;    // DECISION T4: OTTO banks 2-3
constexpr uint8_t kMuxSelect[kAnalogCount] = {0, 2, 3, 4, 5, 6, 7};  // Hardware facts: U69 Y0, Y2-Y7 (Y1 n.c.)
}  // namespace

Machine::Region Machine::decode(uint32_t a) {
    if (a < kRamLowTop || a >= kRamHighBase) return Region::Ram;
    if (a >= kRomBase && a < kRomBase + kRomWindow) return Region::Rom;
    if (a >= kHostBase && a < kHostBase + kHostWindow) return Region::Host;
    if (a >= kIo1Base && a < kIo1Base + kIo1Window) {
        static constexpr Region kIo1[4] = {Region::Duart, Region::Scsi, Region::Fdc, Region::Iexp};
        return kIo1[(a >> 18) & 3];  // U7/A, a 74HC139 on A19:A18
    }
    if (a >= kIo2Base && a < kIo2Base + kIo2Window) return (a & 0x80000) ? Region::Esp : Region::Otto;  // U7/B
    return Region::None;
}

std::unique_ptr<Machine> Machine::create(const RomSet& roms, std::string& err) {
    std::unique_ptr<Machine> m(new Machine());
    if (!buildOsImage(roms, m->os_, err) || !m->waves_.load(roms, err)) return nullptr;
    // DECISION T1: 500 kHz on IP3/IP4 (channel A, MIDI); 1 MHz on IP5 (TxCB) and IP6 (RxCB). The board's
    // 2681 takes RxCB from IP6, the core (a 68681) from IP2: the 1 MHz clock goes on the core's IP2
    // clock input, while the IP2 level stays DENS.
    m->duart_.setExternalClock(3, 500'000);
    m->duart_.setExternalClock(4, 500'000);
    m->duart_.setExternalClock(5, 1'000'000);
    m->duart_.setExternalClock(2, 1'000'000);
    m->duart_.setInputPins(0x3F);  // DECISION T10: empty drive: no index pulse on IP0 (DSTAT); IP1, IP2 high
    for (int i = 0; i < kAnalogCount; ++i) m->mux_[kMuxSelect[i]] = kAnalogRest[i];
    m->powerOn();
    return m;
}

void Machine::powerOn() {
    board_.otto().reset();
    board_.esp().reset();
    duart_.reset();
    fdc_.reset();
    panel_.reset();
    carry_ = 0;
    duartAcc_ = 0;
    overlay_ = true;
    cpu_.reset();  // the 40 reset cycles count in cycles() and are not slot carry
    updateLines();
}

void Machine::setAnalog(Analog a, uint16_t raw10) {
    const int i = static_cast<int>(a);
    if (i < 0 || i >= kAnalogCount) return;
    mux_[kMuxSelect[i]] = std::min<uint16_t>(raw10, 0x3FF);  // a "max" request beyond 10 bits stays max
    updateLines();
}

// Hardware facts: IEXP (U12/A) D0 FSW3B/, D1 FSW4B/, D2 PSRB/, D3 PSLB/, active low.
void Machine::footswitch(int n, bool down) {
    if (n != 3 && n != 4) return;
    const uint8_t bit = static_cast<uint8_t>(1u << (n - 3));
    iexpDown_ = static_cast<uint8_t>(down ? iexpDown_ | bit : iexpDown_ & ~bit);
}

void Machine::patchSelect(int side, bool down) {
    if (side != 0 && side != 1) return;
    const uint8_t bit = side == 0 ? 0x08 : 0x04;
    iexpDown_ = static_cast<uint8_t>(down ? iexpDown_ | bit : iexpDown_ & ~bit);
}

uint8_t Machine::romByte(uint32_t off) const {
    const uint16_t w = os_[(off & (kRomBytes - 1)) >> 1];
    return static_cast<uint8_t>((off & 1) ? w : w >> 8);
}

uint8_t Machine::memByte(uint32_t a, bool& mapped) const {
    mapped = true;
    switch (decode(a)) {
    case Region::Rom: return romByte(a);
    case Region::Ram: {
        const uint32_t i = a & (kRamBytes - 1);  // DECISION T13: 0-7 read the ROM until the first write there
        return (overlay_ && i < 8) ? romByte(i) : ram_[i];
    }
    case Region::Host: {
        const uint16_t w = hostWord(a);
        return static_cast<uint8_t>((a & 1) ? w : w >> 8);
    }
    default: mapped = false; return 0;
    }
}

// DECISION T4: the host window is word (a - base) / 2 of OTTO banks 2-3 (CPU A22..A1); only bank 2
// words 0-0xFFFFF exist (one 1M x 8 SIMM pair). Empty sockets read 0 and ignore writes.
uint16_t Machine::hostWord(uint32_t a) const {
    const uint32_t w = (a - kHostBase) >> 1;
    return w < WaveMemory::kSampleRamWords ? waves_.sampleRam(w) : 0;
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

// DECISION T5: the IPL is the highest level among the asserting devices (OTTO 1, DUART 3; all autovectored).
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
    panel_.setLines((pins & 0x10) != 0, (pins & 0x20) != 0, (pins & 0x80) != 0);  // DECISION T19: KRES, DRES, SACK/
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

// One byte of an I/O device. Odd-byte devices (DUART, OTTO, ESP) see only odd addresses; an even
// byte reads 0 and its write is ignored (traced with its even address).
uint8_t Machine::ioRead(Region r, uint32_t a) {
    const bool odd = (a & 1) != 0;
    switch (r) {
    case Region::Duart: return odd ? duart_.read(static_cast<uint8_t>((a >> 1) & 0xF)) : 0;
    case Region::Otto: return odd ? board_.otto().read(static_cast<uint8_t>((a >> 1) & 0x3F)) : 0;
    case Region::Esp: return odd ? board_.esp().hostRead(static_cast<uint8_t>((a >> 1) & 0xFF)) : 0;
    case Region::Fdc: return odd ? fdc_.read((a & 2) != 0) : 0;  // FDC A0 = CPU A1
    case Region::Iexp: return static_cast<uint8_t>(0xFF & ~iexpDown_);  // DECISION T9: upper bits high
    default: return 0;  // DECISION T12: SCSI (no board) and unmapped space read 0
    }
}

void Machine::ioWrite(Region r, uint32_t a, uint8_t v) {
    if (!(a & 1)) return;
    switch (r) {
    case Region::Duart: duart_.write(static_cast<uint8_t>((a >> 1) & 0xF), v); break;
    case Region::Otto: board_.otto().write(static_cast<uint8_t>((a >> 1) & 0x3F), v); break;
    case Region::Esp: board_.esp().hostWrite(static_cast<uint8_t>((a >> 1) & 0xFF), v); break;
    case Region::Fdc: fdc_.write((a & 2) != 0, v); break;
    default: break;
    }
}

Device Machine::deviceOf(Region r) {
    switch (r) {
    case Region::Duart: return Device::Duart;
    case Region::Otto: return Device::Otto;
    case Region::Esp: return Device::Esp;
    case Region::Fdc: return Device::Fdc;
    case Region::Iexp: return Device::Iexp;
    case Region::Scsi: return Device::Scsi;
    default: return Device::Unmapped;
    }
}

uint8_t Machine::read8(uint32_t a) {
    a &= kMask24;
    bool mapped;
    const uint8_t m = memByte(a, mapped);
    if (mapped) return m;
    const Region r = decode(a);
    const uint8_t v = ioRead(r, a);
    trace(a, v, 1, false, deviceOf(r));
    updateLines();
    return v;
}

// A word access to a byte device: the low (odd) byte is the device, the high byte reads 0.
uint16_t Machine::read16(uint32_t a) {
    a &= kMask24;
    bool mapped;
    const uint8_t hi = memByte(a, mapped);
    if (mapped) return static_cast<uint16_t>((hi << 8) | memByte(a + 1, mapped));
    const Region r = decode(a);
    const uint16_t v = ioRead(r, a | 1);
    trace(a, v, 2, false, deviceOf(r));
    updateLines();
    return v;
}

void Machine::write8(uint32_t a, uint8_t v) {
    a &= kMask24;
    const Region r = decode(a);
    if (r == Region::Rom) return;
    if (r == Region::Host) {
        const uint32_t w = (a - kHostBase) >> 1;
        if (w < WaveMemory::kSampleRamWords) {
            uint16_t& d = waves_.sampleRam(w);
            d = (a & 1) ? static_cast<uint16_t>((d & 0xFF00) | v) : static_cast<uint16_t>((d & 0xFF) | v << 8);
        }
        return;
    }
    if (r == Region::Ram) {
        const uint32_t i = a & (kRamBytes - 1);
        if (i < 8) overlay_ = false;
        ram_[i] = v;
        return;
    }
    ioWrite(r, a, v);
    trace(a, v, 1, true, deviceOf(r));
    updateLines();
}

void Machine::write16(uint32_t a, uint16_t v) {
    a &= kMask24;
    const Region r = decode(a);
    if (r == Region::Rom || r == Region::Ram || r == Region::Host) {
        write8(a, static_cast<uint8_t>(v >> 8));
        write8(a + 1, static_cast<uint8_t>(v));
        return;
    }
    ioWrite(r, a | 1, static_cast<uint8_t>(v));
    trace(a, v, 2, true, deviceOf(r));
    updateLines();
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
    // DECISION T19: the panel takes the bytes that finished leaving TxDB in this slot; the keyboard puts
    // its next byte on RxDB only when the line is idle, so SACK/ can hold it between bytes.
    uint8_t byte = 0;
    while (duart_.popTransmitted(kPanelChannel, byte)) {
        panel_.fromCpu(byte);
        if (onPanelByte) onPanelByte(byte);
    }
    if (duart_.rxQueued(kPanelChannel) == 0 && panel_.popToCpu(byte)) duart_.receive(kPanelChannel, byte);
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
