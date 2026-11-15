#pragma once
// The floppy controller U20 (uPD72069): the uPD765 command/result handshake on its two registers, with an
// empty drive (DECISION T10) or a Disk (DECISION T22). Non-DMA, no timing: every byte is ready on every poll.
#include <cstdint>

#include "ts/disk.h"

namespace ens::ts {

class Fdc {
public:
    void reset();  // keeps the disk pointer, the head position and the data rate

    // A0 = 0: main status register. A0 = 1: the next result byte, or the next byte of a sector being read.
    uint8_t read(bool a0);
    // A0 = 1: a command or parameter byte, or a byte of a sector being written. A0 = 0: the 72069's own
    // control register; only remembered (control()).
    void write(bool a0, uint8_t v);

    void setDisk(Disk* d) { disk_ = d; }  // nullptr: empty drive; the machine owns the disk
    uint8_t control() const { return control_; }
    int cylinder() const { return cyl_; }
    void setHighRate(bool high) { high_ = high; }

private:
    enum class Phase : uint8_t { Command, ExecRead, ExecWrite, ExecFormat, Result };

    static int commandLength(uint8_t c);
    void execute();
    void startTransfer(Phase p);
    void loadSector();
    void nextSector();
    void finishTransfer();
    void finishError(uint8_t st1, uint8_t st2);
    void setResult(uint8_t st0, uint8_t st1, uint8_t st2, uint8_t c, uint8_t h, uint8_t r, uint8_t n);
    bool trackReadable(int cyl, int head) const;  // formatted and at the selected data rate

    Disk* disk_ = nullptr;
    bool high_ = false;
    uint8_t control_ = 0;
    int cyl_ = 0;
    bool intPending_ = false;
    uint8_t intSt0_ = 0;

    Phase phase_ = Phase::Command;
    uint8_t cmd_[9] = {}, result_[7] = {};
    int cmdLen_ = 0, cmdPos_ = 0, resultLeft_ = 0, resultPos_ = 0;
    // transfer state
    uint8_t* sec_ = nullptr;
    int secPos_ = 0, curR_ = 0, idNext_ = 0, fmtLeft_ = 0, fmtPos_ = 0;
    uint8_t fmtId_[4] = {};
};

}  // namespace ens::ts
