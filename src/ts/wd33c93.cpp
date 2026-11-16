#include "ts/wd33c93.h"

namespace ens::ts {

void Wd33c93::reset() {
    for (auto& r : regs_) r = 0;
    addr_ = 0;
    int_ = lci_ = active_ = busFree_ = false;
    target_ = nullptr;
}

uint8_t Wd33c93::read(bool a0) {
    if (!a0) {
        pump();
        // Auxiliary Status, datasheet 3.1.1: INT 7, LCI 6, BSY 5, CIP 4, DBR 0.
        uint8_t s = (int_ ? 0x80 : 0) | (lci_ ? 0x40 : 0);
        if (active_) s |= 0x30;
        if (dataPhase() && target_->ready()) s |= 0x01;
        return s;
    }
    uint8_t v = readReg(addr_);
    // Datasheet 3.1.2: every a0 = 1 cycle steps the address except at Data and Command.
    if (addr_ != kCommand && addr_ != kData) addr_++;
    return v;
}

void Wd33c93::write(bool a0, uint8_t v) {
    if (!a0) { addr_ = v & 0x1F; return; }
    uint8_t r = addr_;
    if (addr_ != kCommand && addr_ != kData) addr_++;
    writeReg(r, v);
}

void Wd33c93::countDown() {
    uint32_t n = (uint32_t(regs_[kCount]) << 16 | regs_[kCount + 1] << 8 | regs_[kCount + 2]) - 1;
    regs_[kCount] = uint8_t(n >> 16);
    regs_[kCount + 1] = uint8_t(n >> 8);
    regs_[kCount + 2] = uint8_t(n);
}

uint8_t Wd33c93::readReg(uint8_t r) {
    if (r >= kRegs) return 0xFF;    // datasheet 3.0 note 2: undefined registers read 0xFF
    if (r == kScsiStatus) {
        uint8_t s = regs_[kScsiStatus];
        int_ = false;
        lci_ = false;
        if (busFree_) { busFree_ = false; raise(0x85); }    // datasheet 4.6.1: bus free after the command
        return s;
    }
    if (r == kData && dataPhase() && target_->ready() && target_->phase() == ScsiTarget::Phase::DataIn) {
        uint8_t b = target_->readByte();
        countDown();
        pump();
        return b;
    }
    return regs_[r];
}

void Wd33c93::writeReg(uint8_t r, uint8_t v) {
    if (r >= kRegs || r == kScsiStatus) return;
    if (r == kCommand) { command(v); return; }
    if (r == kData) {
        if (dataPhase() && target_->ready() && target_->phase() == ScsiTarget::Phase::DataOut) {
            target_->writeByte(v);
            countDown();
            pump();
        }
        return;
    }
    regs_[r] = v;
}

void Wd33c93::command(uint8_t c) {
    if (int_) { lci_ = true; return; }  // datasheet 3.1.1: a command while INT is set is ignored, LCI
    regs_[kCommand] = c;
    active_ = false;
    c &= 0x7F;
    if (c == 0x00) {                    // Reset, datasheet 3.2.1: EAF (Own ID bit 3) gives status 0x01
        target_ = nullptr;
        busFree_ = false;
        raise((regs_[kOwnId] & 0x08) ? 0x01 : 0x00);
    } else if (c == 0x08 || c == 0x09) {    // Select-and-Transfer, with or without ATN
        // DECISION T27: selection, command and status take no time; the Timeout Period is not modelled.
        ScsiTarget* t = targets_[regs_[kDest] & 7];
        if (!t) { regs_[kPhase] = 0x00; raise(0x42); return; }  // selection timeout
        int group = regs_[kCdb] >> 5;
        int len = group == 5 ? 12 : (group == 1 || group == 2) ? 10 : 6;    // CDB size by group
        uint32_t count = uint32_t(regs_[kCount]) << 16 | regs_[kCount + 1] << 8 | regs_[kCount + 2];
        target_ = t;
        active_ = true;
        // DECISION T25: the chip runs the whole select/command/data/status sequence as one state machine.
        t->command(&regs_[kCdb], len, count);
        pump();
    } else {
        raise(0x40);                    // invalid command
    }
}

void Wd33c93::pump() {
    if (!active_ || target_->phase() != ScsiTarget::Phase::Status) return;
    active_ = false;
    regs_[kStatus0F] = target_->status();
    regs_[kPhase] = 0x60;               // command complete
    busFree_ = true;
    raise(0x16);                        // Select-and-Transfer done
}

}  // namespace ens::ts
