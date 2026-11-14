// ES5506 OTTO core (Plan 2). Section numbers cite docs/reference/es5506-otto-datasheet-notes.md.
#include "otto/otto.h"

#include <cstring>

namespace ens {

namespace {

constexpr uint32_t kTest = 0x40;  // PAGE bit 6
// CR bits, notes §2.
constexpr uint32_t kStop0 = 0x1, kStop1 = 0x2, kLei = 0x4, kLpe = 0x8, kBle = 0x10, kIrqe = 0x20, kDir = 0x40,
                   kIrq = 0x80;

}  // namespace

Otto::Otto(OttoMemory& mem) : mem_(mem) { reset(); }

void Otto::reset() {
    // DECISION OT3: only ACTV and MODE have documented reset values; the rest follow the KS-32 O10
    // pattern (voices stopped, everything else 0, IRQV free).
    std::memset(voice_, 0, sizeof voice_);
    for (auto& v : voice_) v.cr = 0x0003;  // STOP1 | STOP0
    actv_ = 0x1F;
    mode_ = 0x17;
    // DECISION OT18: MODE and the serial timing registers are stored and read back only; output is
    // modelled as whole frames, and dual-OTTO/slave modes are not modelled (the TS has one OTTO).
    wst_ = wend_ = lrend_ = 0;
    page_ = 0;
    par_ = 0;
    irqv_ = 0x80;
    std::memset(chTest_, 0, sizeof chTest_);
    std::memset(wlatch_, 0, sizeof wlatch_);
    rlatch_ = 0;
    irqPin_ = false;
    irqClear_ = 0;
    slot_ = 0;
    ready_ = false;
    std::memset(&frame_, 0, sizeof frame_);
    s1_ = s2_ = 0;
}

void Otto::stepVoice() {
    if (slot_ == 0) ready_ = false;
    const int vi = static_cast<int>(slot_);
    Voice& v = voice_[vi];
    // Ruling (OT14): the read-acknowledged voice's CR.IRQ is cleared at the start of its next slot,
    // so a new request raised later in the same slot is kept.
    if (irqClear_ & (1u << vi)) {
        v.cr &= ~kIrq;
        irqClear_ &= ~(1u << vi);
    }
    // DECISION OT5: S1 at the integer address, S2 at integer + 1 (21-bit wrap) in both directions,
    // fetched with the accumulator before this slot's update; bank = BS1:BS0.
    const int bank = static_cast<int>(v.cr >> 14);
    const uint32_t a = v.accum >> 11;
    s1_ = mem_.read(bank, a);
    s2_ = mem_.read(bank, (a + 1) & 0x1FFFFF);
    if (!(v.cr & (kStop0 | kStop1))) updateAddress(v);
    serviceIrq(vi, v);
    if (++slot_ > actv_) {
        slot_ = 0;
        ready_ = true;
    }
}

OttoFrame Otto::takeFrame() {
    ready_ = false;
    return frame_;
}

OttoFrame Otto::runFrame() {
    do stepVoice();
    while (!ready_);
    return takeFrame();
}

// DECISION OT4: FC added once; strict compares (n > END fwd, n < START rev); DIR=0 is forward;
// LEI skips the check (no loop, no IRQ); transwave jumps like a forward loop, then clears BLE and
// sets LEI; a one-shot stops on the boundary it crossed.
void Otto::updateAddress(Voice& v) {
    const int64_t start = v.start & 0xFFFFF800, end = v.end & 0xFFFFFF80;
    const bool rev = v.cr & kDir;
    int64_t n = rev ? int64_t(v.accum) - v.fc : int64_t(v.accum) + v.fc;
    if (!(v.cr & kLei) && (rev ? n < start : n > end)) {
        if (v.cr & kIrqe) v.cr |= kIrq;
        const bool lpe = v.cr & kLpe, ble = v.cr & kBle;
        if (!lpe && !ble) {
            v.cr |= kStop0;
            n = rev ? start : end;
        } else if (lpe && ble) {
            n = rev ? start + (start - n) : end - (n - end);
            v.cr ^= kDir;
        } else {
            n = rev ? end - (start - n) : start + (n - end);
            if (ble) v.cr = (v.cr & ~kBle) | kLei;
        }
    }
    v.accum = static_cast<uint32_t>(n);
}

// DECISION OT14: a voice with CR.IRQ latches into IRQV at its slot when IRQV is free (bit 7 = 1);
// a byte-0 read of IRQV frees it, releases the pin, and clears that voice's CR.IRQ at its next slot.
void Otto::serviceIrq(int vi, Voice& v) {
    if ((v.cr & kIrq) && (irqv_ & 0x80) && !(irqClear_ & (1u << vi))) {
        irqv_ = static_cast<uint32_t>(vi);
        irqPin_ = true;
    }
}

// DECISION OT1: byte 0 of a read performs the 32-bit read into one shared latch; bytes 1-3 return
// it. Bytes 0-2 of a write go to one shared latch; byte 3 commits latch + byte to the register it
// addresses. The latch keeps its bytes after a commit.
uint8_t Otto::read(uint8_t ha) {
    const uint8_t reg = (ha >> 2) & 0xF, byte = ha & 3;
    if (byte == 0) rlatch_ = readReg(reg);
    return static_cast<uint8_t>(rlatch_ >> (24 - 8 * byte));
}

void Otto::write(uint8_t ha, uint8_t data) {
    const uint8_t reg = (ha >> 2) & 0xF, byte = ha & 3;
    if (byte < 3) {
        wlatch_[byte] = data;
        return;
    }
    // DECISION OT17: a write takes effect at once; the datasheet's 3-voice pipeline is not modelled.
    writeReg(reg, uint32_t(wlatch_[0]) << 24 | uint32_t(wlatch_[1]) << 16 | uint32_t(wlatch_[2]) << 8 | data);
}

// DECISION OT2: writes keep each register's implemented bits, reads return them with other bits 0;
// TEST selects the test page whatever PS is; test-page and EMPTY reads return 0.
uint32_t* Otto::voiceReg(uint8_t reg, uint32_t& mask) {
    Voice& v = voice_[page_ & 0x1F];
    if (page_ & 0x20) {
        switch (reg) {
        case 0: mask = 0xFFFF; return &v.cr;
        case 1: mask = 0xFFFFF800; return &v.start;
        case 2: mask = 0xFFFFFF80; return &v.end;
        case 3: mask = 0xFFFFFFFF; return &v.accum;
        case 4: mask = 0x3FFFF; return &v.o4_1;
        case 5: mask = 0x3FFFF; return &v.o3_1;
        case 6: mask = 0x3FFFF; return &v.o3_2;
        case 7: mask = 0x3FFFF; return &v.o2_1;
        case 8: mask = 0x3FFFF; return &v.o2_2;
        case 9: mask = 0x3FFFF; return &v.o1_1;
        case 10: mask = 0x7F; return &wst_;
        case 11: mask = 0x7F; return &wend_;
        case 12: mask = 0x7F; return &lrend_;
        default: return nullptr;
        }
    }
    switch (reg) {
    case 0: mask = 0xFFFF; return &v.cr;
    case 1: mask = 0x1FFFF; return &v.fc;
    case 2: mask = 0xFFFF; return &v.lvol;
    case 3: mask = 0xFF00; return &v.lvramp;
    case 4: mask = 0xFFFF; return &v.rvol;
    case 5: mask = 0xFF00; return &v.rvramp;
    case 6: mask = 0x1FF; return &v.ecount;
    case 7: mask = 0xFFFF; return &v.k2;
    case 8: mask = 0xFF01; return &v.k2ramp;
    case 9: mask = 0xFFFF; return &v.k1;
    case 10: mask = 0xFF01; return &v.k1ramp;
    case 11: mask = 0x1F; return &actv_;
    case 12: mask = 0x1F; return &mode_;
    default: return nullptr;
    }
}

uint32_t Otto::readReg(uint8_t reg) {
    switch (reg) {
    case 13: return par_;
    case 14: {
        const uint32_t r = irqv_;
        if (!(irqv_ & 0x80)) {
            irqClear_ |= 1u << (irqv_ & 0x1F);
            irqv_ |= 0x80;
            irqPin_ = false;
        }
        return r;
    }
    case 15: return page_;
    default: break;
    }
    if (page_ & kTest) return 0;  // channel registers are write-only, EMPTY reads 0
    uint32_t mask = 0;
    const uint32_t* r = voiceReg(reg, mask);
    return r ? *r & mask : 0;
}

void Otto::writeReg(uint8_t reg, uint32_t v) {
    switch (reg) {
    case 13:  // PAR and IRQV are read-only
    case 14: return;
    case 15: page_ = v & 0x7F; return;
    default: break;
    }
    if (page_ & kTest) {
        if (reg < 12) chTest_[reg] = v & 0x7FFFFF;
        return;
    }
    uint32_t mask = 0;
    if (uint32_t* r = voiceReg(reg, mask)) *r = v & mask;
}

}  // namespace ens
