// The SCSI target side as the WD33C93 core sees it (Plan 7, design "The machine"). The host starts a command with
// its Transfer Count, then moves bytes one by one while phase() is DataIn or DataOut and ready(), then reads status().
#pragma once
#include <cstdint>

namespace ens::ts {

class ScsiTarget {
public:
    enum class Phase : uint8_t { DataIn, DataOut, Status };
    virtual ~ScsiTarget() = default;
    // A new command with the host's Transfer Count. DataIn yields exactly `count` bytes (padded with 0 or cut).
    virtual void command(const uint8_t* cdb, int len, uint32_t count) = 0;
    virtual Phase phase() const = 0;
    virtual bool ready() const = 0;          // the next data byte can move (false while a sector is on its way)
    virtual uint8_t readByte() = 0;          // DataIn
    virtual void writeByte(uint8_t v) = 0;   // DataOut
    virtual uint8_t status() const = 0;      // in Status: 0x00 GOOD, 0x02 CHECK CONDITION
};

}  // namespace ens::ts
