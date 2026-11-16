// SCSI CD-ROM target (ID 4; Plan 7, DECISION T28). Read-only. Sectors come from a SectorPort, never from a file.
#pragma once
#include <cstdint>

#include "ts/scsi.h"
#include "ts/sector_port.h"

namespace ens::ts {

class CdTarget : public ScsiTarget {
public:
    explicit CdTarget(SectorPort& port) : port_(port) {}
    // sectors 0: eject. Medium ids are chosen by the caller (0 = none). Any change queues one UNIT ATTENTION.
    void insert(uint32_t medium, uint32_t sectors);
    bool hasDisc() const { return sectors_ != 0; }
    uint32_t medium() const { return medium_; }
    uint32_t sectors() const { return sectors_; }

    void command(const uint8_t* cdb, int len, uint32_t count) override;
    Phase phase() const override;
    bool ready() const override;
    uint8_t readByte() override;
    void writeByte(uint8_t v) override;
    uint8_t status() const override { return status_; }

private:
    enum class Src : uint8_t { Buf, Read };
    void check(uint8_t key, uint8_t asc) { status_ = 2; key_ = key; asc_ = asc; }
    void applyModeSelect();

    SectorPort& port_;
    uint32_t medium_ = 0, sectors_ = 0, blockLen_ = 2048;
    bool attention_ = false;
    mutable uint8_t key_ = 0, asc_ = 0;
    mutable uint8_t status_ = 0;

    // the command in flight
    bool dataOut_ = false;
    Src src_ = Src::Buf;
    uint32_t remaining_ = 0;       // host bytes still to move
    uint32_t pos_ = 0, avail_ = 0; // bytes moved so far, bytes the command really has
    uint64_t base_ = 0;            // byte position of the first block of a READ on the medium
    uint8_t resp_[32] = {};
    uint8_t out_[16] = {};         // MODE SELECT parameter bytes
    uint32_t outLen_ = 0;

    // the sector held (copied out of the port) and the one asked for
    mutable bool secValid_ = false, asked_ = false, failed_ = false;
    mutable uint32_t secIdx_ = 0;
    mutable uint8_t sec_[SectorPort::kBytes] = {};
};

}  // namespace ens::ts
