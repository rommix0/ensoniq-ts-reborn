#include "ts/fdc.h"

#include <algorithm>

// DECISION T22: the uPD765 command set the OS uses, against a Disk. Details at each point of use; docs/decisions.md.

namespace ens::ts {

void Fdc::reset() {
    Disk* d = disk_;
    const bool h = high_;
    const int c = cyl_;  // the head does not move on a reset
    *this = Fdc{};
    disk_ = d;
    high_ = h;
    cyl_ = c;
}

// DECISION T22: a disk removed or changed during an execution phase ends that command as a drive losing ready
// does: ST0 IC 01 + NR, C H R N of where it stopped; no pointer into either disk survives. Outside an execution
// phase only the pointer changes.
void Fdc::setDisk(Disk* d) {
    if (d != disk_ && (phase_ == Phase::ExecRead || phase_ == Phase::ExecWrite || phase_ == Phase::ExecFormat))
        setResult(static_cast<uint8_t>(0x48 | (cmd_[1] & 7)), 0, 0, cmd_[2], cmd_[3], static_cast<uint8_t>(curR_), cmd_[5]);
    disk_ = d;
}

int Fdc::commandLength(uint8_t c) {
    switch (c & 0x1F) {
    case 0x03: return 3;  // specify
    case 0x04: return 2;  // sense drive status
    case 0x07: return 2;  // recalibrate
    case 0x08: return 1;  // sense interrupt status
    case 0x0F: return 3;  // seek
    case 0x0A: return 2;  // read ID
    case 0x0D: return 6;  // format track
    case 0x02: case 0x05: case 0x06: case 0x09: case 0x0C: case 0x11: case 0x19: case 0x1D: return 9;
    default: return 1;    // invalid
    }
}

// DECISION T22: multi-sector transfers advance R; the transfer ends after sector EOT or at the end of the
// track. The result's R is one past the last sector.
void Fdc::nextSector() {
    if (curR_ >= cmd_[6] || curR_ + 1 >= disk_->sectorsPerTrack()) {
        ++curR_;
        finishTransfer();
    } else {
        ++curR_;
        loadSector();
    }
}

uint8_t Fdc::read(bool a0) {
    if (!a0) {
        switch (phase_) {
        case Phase::ExecRead: return 0xF0;                           // RQM, DIO, EXM, CB
        case Phase::ExecWrite: case Phase::ExecFormat: return 0xB0;  // RQM, EXM, CB
        default: return static_cast<uint8_t>(0x80 | (resultLeft_ ? 0x50 : 0) | (cmdLen_ ? 0x10 : 0));
        }
    }
    if (phase_ == Phase::ExecRead) {
        const uint8_t v = sec_[secPos_++];
        if (secPos_ >= Disk::kSectorBytes) {
            disk_->noteRead((cmd_[2] * Disk::kHeads + cmd_[3]) * disk_->sectorsPerTrack() + curR_);
            nextSector();
        }
        return v;
    }
    if (!resultLeft_) return 0;
    const uint8_t v = result_[resultPos_++];
    if (--resultLeft_ == 0) phase_ = Phase::Command;
    return v;
}

void Fdc::write(bool a0, uint8_t v) {
    if (!a0) {
        // DECISION T24: 0x36 is the 72069's software reset (the OS's recovery: 0x36, a pause, then Sense
        // Interrupt Status); 0x88 / 0xC8 select the DD / HD data rate (the OS writes them from DENS). The rest
        // (0x0B, 0x4F, 0xE3/0xF3, 0x1E/0x0E) are only remembered.
        if (v == 0x36) reset();
        else if ((v & 0x3F) == 0x08) high_ = (v & 0x40) != 0;
        control_ = v;
        return;
    }
    if (phase_ == Phase::ExecWrite) {
        sec_[secPos_++] = v;
        if (secPos_ >= Disk::kSectorBytes) {
            disk_->noteWrite();  // one write per sector
            nextSector();
        }
        return;
    }
    if (phase_ == Phase::ExecFormat) {
        fmtId_[fmtPos_++] = v;  // C H R N of each sector; only the count matters (the Disk has fixed ids)
        if (fmtPos_ >= 4) { fmtPos_ = 0; --fmtLeft_; }
        if (fmtLeft_ <= 0) {
            // DECISION T22: Format Track formats the track under the head with the filler byte D, whatever
            // C H R N the IDs carried; the result's C H R N are the last ID received.
            disk_->formatTrack(cyl_, (cmd_[1] >> 2) & 1, cmd_[5]);
            setResult(cmd_[1] & 7, 0, 0, fmtId_[0], fmtId_[1], fmtId_[2], fmtId_[3]);
        }
        return;
    }
    if (phase_ == Phase::ExecRead) return;
    if (!cmdLen_) {
        resultLeft_ = 0;
        phase_ = Phase::Command;
        cmdLen_ = commandLength(v);
        cmdPos_ = 0;
    }
    if (cmdPos_ < static_cast<int>(sizeof cmd_)) cmd_[cmdPos_] = v;
    if (++cmdPos_ >= cmdLen_) execute();
}

void Fdc::setResult(uint8_t st0, uint8_t st1, uint8_t st2, uint8_t c, uint8_t h, uint8_t r, uint8_t n) {
    result_[0] = st0; result_[1] = st1; result_[2] = st2;
    result_[3] = c; result_[4] = h; result_[5] = r; result_[6] = n;
    resultPos_ = 0;
    resultLeft_ = 7;
    phase_ = Phase::Result;
    sec_ = nullptr;
}

// DECISION T22: TC is tied to ground on the TS (schematic sheet 3, U20), so a transfer that runs to its end
// stops as a uPD765 without TC does: ST0 IC 01 (0x40) and ST1 EN (0x80), which the OS takes as success
// (c0ec40: ST1 exactly 0x80), then C H R N.
void Fdc::finishTransfer() {
    setResult(static_cast<uint8_t>(0x40 | (cmd_[1] & 7)), 0x80, 0, cmd_[2], cmd_[3], static_cast<uint8_t>(curR_),
              cmd_[5]);
}

// DECISION T22: abnormal end = ST0 IC 01 (0x40); ST1/ST2 per the error: MA 0x01 (unformatted track, or a disk
// of the other density: no address mark), ND 0x04 (no such sector), NW 0x02 (write protected); C H R N echo
// the command.
void Fdc::finishError(uint8_t st1, uint8_t st2) {
    setResult(static_cast<uint8_t>(0x40 | (cmd_[1] & 7)), st1, st2, cmd_[2], cmd_[3], static_cast<uint8_t>(curR_),
              cmd_[5]);
}

// DECISION T22: the data rate must match the disk, else no address mark (MA). The default rate is DD.
bool Fdc::trackReadable(int cyl, int head) const {
    return disk_->trackFormatted(cyl, head) && (disk_->density() == Density::High) == high_;
}

// DECISION T22: the command's C names the cylinder read or written (the OS seeks there first; the head
// position is not compared). Ids are 0..N-1 as on Ensoniq disks. A cylinder, head or sector the disk does not
// have, or N other than 2, is "no data" (ND).
void Fdc::loadSector() {
    const int head = cmd_[3];
    if (cmd_[5] != 2 || cmd_[2] >= disk_->cylinders() || head > 1) {
        finishError(0x04, 0);
        return;
    }
    sec_ = disk_->sector(cmd_[2], head, curR_);
    secPos_ = 0;
    if (!sec_) finishError(0x04, 0);
}

void Fdc::startTransfer(Phase p) {
    curR_ = cmd_[4];
    if (!trackReadable(cmd_[2], cmd_[3] & 1)) { finishError(0x01, 0); return; }
    if (p == Phase::ExecWrite && disk_->writeProtected()) { finishError(0x02, 0); return; }
    phase_ = p;
    loadSector();
}

void Fdc::execute() {
    const uint8_t c = cmd_[0] & 0x1F;
    resultPos_ = 0;
    cmdLen_ = 0;
    phase_ = Phase::Command;
    resultLeft_ = 0;
    switch (c) {
    case 0x03: break;
    case 0x07: case 0x0F:
        if (disk_) {
            // DECISION T22: no timing: Seek and Recalibrate finish at once and leave ST0 = SE (0x20) + unit
            // for Sense Interrupt Status, PCN = the new cylinder (clamped to the disk's last, T30).
            cyl_ = c == 0x07 ? 0 : std::min<int>(cmd_[2], disk_->cylinders() - 1);
            intPending_ = true;
            intSt0_ = static_cast<uint8_t>(0x20 | (cmd_[1] & 3));
        }
        break;
    case 0x04:
        result_[0] = static_cast<uint8_t>(cmd_[1] & 7);  // empty drive: drive bits only, not ready
        if (disk_) {
            // DECISION T22: ST3 with a disk: ready 0x20, two-sided 0x08, track 0 0x10, write protect 0x40.
            result_[0] |= static_cast<uint8_t>(0x20 | 0x08 | (cyl_ == 0 ? 0x10 : 0) |
                                               (disk_->writeProtected() ? 0x40 : 0));
        }
        resultLeft_ = 1;
        phase_ = Phase::Result;
        break;
    case 0x08:
        if (intPending_) {
            intPending_ = false;
            result_[0] = intSt0_;
            result_[1] = static_cast<uint8_t>(cyl_);
            resultLeft_ = 2;
        } else {
            result_[0] = 0x80;  // no interrupt pending
            resultLeft_ = 1;
        }
        phase_ = Phase::Result;
        break;
    case 0x0A: case 0x0D: case 0x02: case 0x05: case 0x06: case 0x09: case 0x0C: case 0x11: case 0x19:
    case 0x1D:
        if (!disk_) {
            // Not ready: abnormal termination, ST0 = IC 01 + NR, ST1/ST2 0, then C H R N echoed.
            setResult(static_cast<uint8_t>(0x48 | (cmd_[1] & 7)), 0, 0, cmd_[2], cmd_[3], cmd_[4], cmd_[5]);
        } else if (c == 0x06) {
            startTransfer(Phase::ExecRead);
        } else if (c == 0x05) {
            startTransfer(Phase::ExecWrite);
        } else if (c == 0x0D) {
            const uint8_t head = (cmd_[1] >> 2) & 1;
            if (disk_->writeProtected()) {
                setResult(static_cast<uint8_t>(0x40 | (cmd_[1] & 7)), 0x02, 0, 0, head, 0, 2);
            } else {
                fmtLeft_ = cmd_[3] ? cmd_[3] : 1;  // SC sectors, 4 ID bytes each
                fmtPos_ = 0;
                phase_ = Phase::ExecFormat;
            }
        } else if (c == 0x0A) {
            // DECISION T22: Read ID answers the next sector of the track, cycling through the ids, with the
            // head's cylinder and the command's head; unformatted or wrong rate gives MA.
            const uint8_t head = (cmd_[1] >> 2) & 1;
            if (!trackReadable(cyl_, head)) {
                setResult(static_cast<uint8_t>(0x40 | (cmd_[1] & 7)), 0x01, 0, static_cast<uint8_t>(cyl_), head, 0, 2);
            } else {
                setResult(cmd_[1] & 7, 0, 0, static_cast<uint8_t>(cyl_), head, static_cast<uint8_t>(idNext_), 2);
                idNext_ = (idNext_ + 1) % disk_->sectorsPerTrack();
            }
        } else {
            // DECISION T22: Read Track, deleted-data and Scan commands are not implemented: abnormal end, MA.
            setResult(static_cast<uint8_t>(0x40 | (cmd_[1] & 7)), 0x01, 0, 0, 0, 0, 0);
        }
        break;
    default:
        result_[0] = 0x80;  // invalid command
        resultLeft_ = 1;
        phase_ = Phase::Result;
        break;
    }
}

}  // namespace ens::ts
