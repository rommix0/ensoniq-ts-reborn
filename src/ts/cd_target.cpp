#include "ts/cd_target.h"

#include <cstring>

namespace ens::ts {

namespace {

// DECISION T28: the CD answers. The service manual (p. 62) only says "any CD-ROM that uses a Sony or Toshiba
// mechanism should work" and names no model, so the strings are a Toshiba XM-3401 style pair (8 + 16 chars).
const char kVendor[8 + 1] = "TOSHIBA ";
const char kProduct[16 + 1] = "CD-ROM XM-3401TA";
const char kRevision[4 + 1] = "3605";

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void put32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }

}  // namespace

void CdTarget::insert(uint32_t medium, uint32_t sectors) {
    const bool moving = remaining_ != 0;
    medium_ = sectors ? medium : 0;
    sectors_ = sectors;
    blockLen_ = 2048;
    attention_ = true;
    remaining_ = 0;
    // DECISION T28: a command still moving data ends CHECK CONDITION, never GOOD on a short block: NOT READY
    // (2/0x3A) for an eject, UNIT ATTENTION (6/0x28) for a change, which that report also consumes.
    if (moving) {
        if (sectors) { attention_ = false; check(6, 0x28); }
        else check(2, 0x3A);
    }
    secValid_ = asked_ = failed_ = false;
}

ScsiTarget::Phase CdTarget::phase() const {
    if (remaining_ == 0) return Phase::Status;
    return dataOut_ ? Phase::DataOut : Phase::DataIn;
}

void CdTarget::command(const uint8_t* cdb, int len, uint32_t count) {
    dataOut_ = false;
    src_ = Src::Buf;
    remaining_ = count;
    pos_ = avail_ = outLen_ = 0;
    secValid_ = asked_ = failed_ = false;
    status_ = 0;
    const uint8_t op = len > 0 ? cdb[0] : 0xFF;
    auto fail = [&](uint8_t key, uint8_t asc) { check(key, asc); remaining_ = 0; };

    if (op == 0x12) {  // INQUIRY: answered whatever the disc and attention state
        std::memset(resp_, 0, sizeof resp_);
        resp_[0] = 5;     // CD-ROM
        resp_[1] = 0x80;  // removable
        resp_[2] = 2;     // ANSI version 2
        resp_[3] = 2;
        resp_[4] = 31;
        std::memcpy(resp_ + 8, kVendor, 8);
        std::memcpy(resp_ + 16, kProduct, 16);
        std::memcpy(resp_ + 32, kRevision, 4);
        avail_ = 36;
        return;
    }
    if (op == 0x03) {  // REQUEST SENSE: fixed format; reading it clears the sense
        std::memset(resp_, 0, sizeof resp_);
        resp_[0] = 0x70;
        resp_[2] = key_;
        resp_[7] = 10;
        resp_[12] = asc_;
        avail_ = 18;
        key_ = asc_ = 0;
        return;
    }
    key_ = asc_ = 0;
    if (attention_) { attention_ = false; fail(6, 0x28); return; }
    const bool needDisc = op == 0x00 || op == 0x25 || op == 0x28 || op == 0x2A;
    if (needDisc && !hasDisc()) { fail(2, 0x3A); return; }

    switch (op) {
    case 0x00: remaining_ = 0; break;  // TEST UNIT READY
    case 0x1A:                         // MODE SENSE(6): header and one block descriptor
        std::memset(resp_, 0, sizeof resp_);
        resp_[0] = 11;
        resp_[3] = 8;
        resp_[9] = uint8_t(blockLen_ >> 16);
        resp_[10] = uint8_t(blockLen_ >> 8);
        resp_[11] = uint8_t(blockLen_);
        avail_ = 12;
        break;
    case 0x15:  // MODE SELECT(6): the parameter bytes arrive through writeByte
        dataOut_ = true;
        if (count == 0) remaining_ = 0;
        break;
    case 0x25:  // READ CAPACITY: last block and block length in the current block length
        put32(resp_, uint32_t((uint64_t(sectors_) * 2048 / blockLen_) - 1));
        put32(resp_ + 4, blockLen_);
        avail_ = 8;
        break;
    case 0x28:
        if (len < 10) { fail(5, 0x20); break; }
        src_ = Src::Read;
        base_ = uint64_t(be32(cdb + 2)) * blockLen_;
        avail_ = uint32_t(uint32_t(cdb[7]) << 8 | cdb[8]) * blockLen_;
        break;
    case 0x2A: fail(7, 0x27); break;  // WRITE(10): DATA PROTECT
    default: fail(5, 0x20); break;    // INVALID COMMAND OPERATION CODE
    }
}

void CdTarget::writeByte(uint8_t v) {
    if (!dataOut_ || remaining_ == 0) return;
    if (outLen_ < sizeof out_) out_[outLen_] = v;
    outLen_++;
    if (--remaining_ == 0) applyModeSelect();
}

void CdTarget::applyModeSelect() {
    // Header byte 3 is the block descriptor length; the descriptor's block length is its bytes 5..7.
    if (outLen_ < 12 || out_[3] < 8) return;
    uint32_t bl = uint32_t(out_[9]) << 16 | uint32_t(out_[10]) << 8 | out_[11];
    if (bl == 512 || bl == 2048) blockLen_ = bl;
    else check(5, 0x26);  // INVALID FIELD IN PARAMETER LIST
}

bool CdTarget::ready() const {
    if (src_ != Src::Read || dataOut_ || remaining_ == 0 || failed_ || pos_ >= avail_) return true;
    const uint32_t need = uint32_t((base_ + pos_) / SectorPort::kBytes);
    if (secValid_ && secIdx_ == need) return true;
    if (!asked_ || secIdx_ != need) {
        port_.ask(medium_, need);
        asked_ = true;
        secIdx_ = need;
        secValid_ = false;
    }
    bool ok = false;
    if (!port_.answered(medium_, need, ok)) return false;
    if (ok) {
        std::memcpy(sec_, port_.data(), SectorPort::kBytes);  // copy out before anything is asked again
        secValid_ = true;
    } else {
        failed_ = true;  // Review Focus 1: the data phase still completes (zeros), the status says MEDIUM ERROR
        status_ = 2;
        key_ = 3;
        asc_ = 0x11;
    }
    return true;
}

uint8_t CdTarget::readByte() {
    if (dataOut_ || remaining_ == 0) return 0;
    uint8_t b = 0;
    if (pos_ < avail_) {
        if (src_ == Src::Buf) b = resp_[pos_];
        else if (!failed_ && secValid_) b = sec_[(base_ + pos_) % SectorPort::kBytes];
    }
    pos_++;
    remaining_--;
    return b;
}

}  // namespace ens::ts
