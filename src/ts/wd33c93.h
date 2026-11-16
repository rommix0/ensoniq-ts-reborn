// WD33C93 SCSI controller as the TS's SP-4 board uses it (Plan 7, DECISION T25, T27): indirect addressing, polled
// (no INTRQ reaches the CPU), Reset and Select-and-Transfer only. No clock: everything happens at register accesses.
#pragma once
#include <cstdint>

#include "ts/scsi.h"

namespace ens::ts {

class Wd33c93 {
public:
    Wd33c93() { reset(); }
    void reset();                       // hardware reset, datasheet 3.2.1
    uint8_t read(bool a0);              // a0 false: Auxiliary Status; true: the addressed register
    void write(bool a0, uint8_t v);     // a0 false: address register; true: the addressed register
    void attach(int id, ScsiTarget* t) { if (id >= 0 && id < 8) targets_[id] = t; }
    bool interrupt() const { return int_; }
    uint8_t reg(int r) const { return regs_[r]; }   // tests
    uint8_t address() const { return addr_; }       // tests

private:
    enum { kOwnId = 0x00, kCdb = 0x03, kStatus0F = 0x0F, kPhase = 0x10, kCount = 0x12, kDest = 0x15,
           kScsiStatus = 0x17, kCommand = 0x18, kData = 0x19, kRegs = 0x1B };
    uint8_t readReg(uint8_t r);
    void writeReg(uint8_t r, uint8_t v);
    void command(uint8_t c);
    void raise(uint8_t status) { regs_[kScsiStatus] = status; int_ = true; }
    void pump();                        // finishes the transfer once the target reaches its Status phase
    void countDown();
    bool dataPhase() const { return active_ && target_->phase() != ScsiTarget::Phase::Status; }

    uint8_t regs_[kRegs];
    uint8_t addr_ = 0;
    bool int_ = false, lci_ = false, active_ = false, busFree_ = false;
    ScsiTarget* targets_[8] = {};
    ScsiTarget* target_ = nullptr;
};

}  // namespace ens::ts
