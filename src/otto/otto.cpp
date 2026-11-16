// ES5506 OTTO core (Plan 2). Section numbers cite docs/reference/es5506-otto-datasheet-notes.md.
#include "otto/otto.h"

#include "common/bits.h"

#include <cstring>

namespace ens {

namespace {

constexpr uint32_t kTest = 0x40;  // PAGE bit 6
// CR bits, notes §2.
constexpr uint32_t kStop0 = 0x1, kStop1 = 0x2, kLei = 0x4, kLpe = 0x8, kBle = 0x10, kIrqe = 0x20, kDir = 0x40,
                   kIrq = 0x80;

uint32_t store18(int32_t v) { return static_cast<uint32_t>(v) & 0x3FFFF; }

}  // namespace

// DECISION OT6: the datasheet gives only the encoder; the decoder restores the stripped sign as the
// inverse of mantissa bit 12 (exp >= 1) and shifts right by 7 - exp; exp 0 keeps the sign in place.
int16_t otto_decompress(uint16_t word) {
    const int e = word >> 13;
    const uint32_t m = word & 0x1FFF;
    const uint32_t raw = e >= 1 ? ((((m >> 12) & 1) ^ 1) << 15) | (m << 2) : (m << 3);
    return static_cast<int16_t>(static_cast<int16_t>(raw & 0xFFFF) >> (e >= 1 ? 7 - e : 7));
}

// DECISION OT8: f9 = fraction bits F10..F2; the result keeps one fraction bit (half-LSB units, the
// filter registers' FDF0) and rounds down.
int32_t otto_interp(int16_t s1, int16_t s2, uint32_t f9) {
    return (int32_t(s1) * 2) + ((int32_t(s2 - s1) * int32_t(f9 & 0x1FF)) >> 8);
}

// DECISION OT9: K is the 12 MSBs over 4096; each pole saturates to 18 bits; the HP pole's feedback is
// 0.5 + K/131072, (k12 + 4096) / 8192: the datasheet's 0.5 floor, below 1 over the whole K range
// (the TS firmware sets K2 up to 0xFF00 on HP poles).
int32_t otto_lp(int32_t x, int32_t y1, uint16_t k12) {
    return sat(int64_t(y1) + ((int64_t(k12) * (x - y1)) >> 12), 18);
}

int32_t otto_hp(int32_t x, int32_t x1, int32_t y1, uint16_t k12) {
    return sat(int64_t(x) - x1 + (((int64_t(k12) + 4096) * y1) >> 13), 18);
}

// DECISION OT12: multiply by 1MMMMMMMM, keep the 20 MSBs (>> 5), shift right by 15 - exponent;
// exponent 0 is a shift of 15, not a mute.
int32_t otto_vol(int32_t x16, uint16_t vol) {
    const int32_t mult = 0x100 | ((vol >> 4) & 0xFF);
    return ((x16 * mult) >> 5) >> (15 - (vol >> 12));
}

// DECISION OT15: the signed 8-bit ramp adds at the register's LSB (the fine bits), saturating to
// 0..0xFFFF.
uint16_t otto_ramp(uint16_t v, uint8_t r8) {
    const int32_t n = int32_t(v) + static_cast<int8_t>(r8);
    return static_cast<uint16_t>(n < 0 ? 0 : (n > 0xFFFF ? 0xFFFF : n));
}

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
    std::memset(sum_, 0, sizeof sum_);
    potSlot_ = 0;
}

void Otto::stepVoice() {
    if (slot_ == 0) ready_ = false;
    // DECISION OT16: POT_RES is a free-running clock of 2^13 CLKs (512 slots of 16 CLKs); each period's end
    // loads the pin's value into PAR.
    if (++potSlot_ == 512) {
        potSlot_ = 0;
        par_ = pot_;
    }
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
    const int32_t x16 = filterVoice(v);
    // DECISION OT7: a stopped voice still fetches and filters but adds nothing, and its envelope
    // and address are frozen (KS-32 O7 behaviour; the datasheet only says its volume is 0).
    const bool running = !(v.cr & (kStop0 | kStop1));
    if (running) {
        // DECISION OT13: CA 0-5 add into that channel; CA 6/7 add nothing.
        const uint32_t ca = (v.cr >> 10) & 7;
        if (ca < 6) {
            sum_[ca][0] += otto_vol(x16, static_cast<uint16_t>(v.lvol));
            sum_[ca][1] += otto_vol(x16, static_cast<uint16_t>(v.rvol));
        }
        envelope(v);
    }
    v.filtcount = (v.filtcount + 1) & 7;
    if (running) updateAddress(v);
    serviceIrq(vi, v);
    // DECISION OT19: ACTV below 4 (fewer than the datasheet's 5 voices) still runs ACTV+1 slots.
    if (++slot_ > actv_) {
        latchFrame();
        slot_ = 0;
        ready_ = true;
    }
}

// DECISION OT15: per voice, only while running: with ECOUNT != 0 both volumes ramp, each K ramps
// unless its own SLOW bit is set and FILTCOUNT != 0, and ECOUNT counts down. FILTCOUNT is a 3-bit
// per-voice counter advanced every slot (the datasheet's K2 line testing K1SLOW is read as K2SLOW).
void Otto::envelope(Voice& v) {
    if (v.ecount == 0) return;
    v.lvol = otto_ramp(static_cast<uint16_t>(v.lvol), static_cast<uint8_t>(v.lvramp >> 8));
    v.rvol = otto_ramp(static_cast<uint16_t>(v.rvol), static_cast<uint8_t>(v.rvramp >> 8));
    if (!(v.k1ramp & 1) || v.filtcount == 0)
        v.k1 = otto_ramp(static_cast<uint16_t>(v.k1), static_cast<uint8_t>(v.k1ramp >> 8));
    if (!(v.k2ramp & 1) || v.filtcount == 0)
        v.k2 = otto_ramp(static_cast<uint16_t>(v.k2), static_cast<uint8_t>(v.k2ramp >> 8));
    --v.ecount;
}

// DECISION OT13: sums are kept in 32 bits (no 23-bit wrap) and saturated to 20 bits at the frame
// end; with TEST set the frame carries the host-written CH values instead and voice sums are dropped.
void Otto::latchFrame() {
    for (int c = 0; c < 6; ++c)
        for (int s = 0; s < 2; ++s) {
            const int32_t v = (page_ & kTest) ? sext(chTest_[c * 2 + s], 23) : sum_[c][s];
            frame_.ch[c][s] = sat(v, 20);
            sum_[c][s] = 0;
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

int32_t Otto::filterVoice(Voice& v) {
    const bool cmpd = v.cr & 0x2000;
    const auto s1 = cmpd ? otto_decompress(s1_) : static_cast<int16_t>(s1_);
    const auto s2 = cmpd ? otto_decompress(s2_) : static_cast<int16_t>(s2_);
    const int32_t x = otto_interp(s1, s2, (v.accum >> 2) & 0x1FF);
    const auto k1 = static_cast<uint16_t>(v.k1 >> 4), k2 = static_cast<uint16_t>(v.k2 >> 4);
    // DECISION OT10: poles 1-2 LP K1; pole 3 by the p9 table (00 HP K2, 01 LP K1, 10 LP K2,
    // 11 LP K1), HP taking Xn-1 = O2(n-2); pole 4 LP K2 or HP K2 with Xn-1 = O3(n-2).
    const int32_t y1 = otto_lp(x, sext(v.o1_1, 18), k1);
    const int32_t y2 = otto_lp(y1, sext(v.o2_1, 18), k1);
    v.o2_2 = v.o2_1;
    v.o2_1 = store18(y2);
    const bool lp3 = v.cr & 0x100, lp4 = v.cr & 0x200;
    const int32_t o3 = sext(v.o3_1, 18);
    const int32_t y3 = lp3 ? otto_lp(y2, o3, k1) : lp4 ? otto_lp(y2, o3, k2) : otto_hp(y2, sext(v.o2_2, 18), o3, k2);
    v.o3_2 = v.o3_1;
    v.o3_1 = store18(y3);
    const int32_t o4 = sext(v.o4_1, 18);
    const int32_t y4 = lp4 ? otto_lp(y3, o4, k2) : otto_hp(y3, sext(v.o3_2, 18), o4, k2);
    v.o1_1 = store18(y1);
    v.o4_1 = store18(y4);
    // DECISION OT11: the pan stage gets the 16-bit integer part of pole 4.
    return sat(y4 >> 1, 16);
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
    case 13:  // DECISION OT16: a read returns the last conversion and starts none
        return par_;
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
