#pragma once
// The floppy controller U20 (uPD72069) with an empty drive: the uPD765 command/result handshake on its
// two registers. DECISION T10: the answers below; "no disk" itself is the missing index pulse on DUART
// IP0, which no ST3 value changes (docs/ts-hardware-interface.md, "Floppy controller and the empty drive").
#include <cstdint>

namespace ens::ts {

class Fdc {
public:
    void reset() { *this = Fdc{}; }

    // A0 = 0: main status register (RQM always; DIO and CB while result bytes wait or a command is open).
    // A0 = 1: the next result byte.
    uint8_t read(bool a0) {
        if (!a0) return static_cast<uint8_t>(0x80 | (resultLeft_ ? 0x50 : 0) | (cmdLen_ ? 0x10 : 0));
        if (!resultLeft_) return 0;
        const uint8_t v = result_[resultPos_++];
        --resultLeft_;
        return v;
    }

    // A0 = 1: a command or parameter byte. A0 = 0 writes are not part of the uPD765 set and are ignored.
    void write(bool a0, uint8_t v) {
        if (!a0) return;
        if (!cmdLen_) {
            resultLeft_ = 0;
            cmdLen_ = commandLength(v);
            cmdPos_ = 0;
        }
        if (cmdPos_ < static_cast<int>(sizeof cmd_)) cmd_[cmdPos_] = v;
        if (++cmdPos_ >= cmdLen_) execute();
    }

private:
    static int commandLength(uint8_t c) {
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

    void execute() {
        const uint8_t c = cmd_[0] & 0x1F;
        resultPos_ = 0;
        cmdLen_ = 0;
        switch (c) {
        case 0x03: case 0x07: case 0x0F: resultLeft_ = 0; break;
        case 0x04: result_[0] = static_cast<uint8_t>(cmd_[1] & 7); resultLeft_ = 1; break;  // ST3: not ready
        case 0x08: result_[0] = 0x80; resultLeft_ = 1; break;  // no interrupt pending
        case 0x0A: case 0x0D: case 0x02: case 0x05: case 0x06: case 0x09: case 0x0C: case 0x11: case 0x19:
        case 0x1D:
            // Not ready: abnormal termination, ST0 = IC 01 + NR, ST1/ST2 0, then C H R N echoed.
            result_[0] = static_cast<uint8_t>(0x48 | (cmd_[1] & 7));
            result_[1] = result_[2] = 0;
            for (int i = 0; i < 4; ++i) result_[3 + i] = cmd_[2 + i];
            resultLeft_ = 7;
            break;
        default: result_[0] = 0x80; resultLeft_ = 1; break;  // invalid command
        }
    }

    uint8_t cmd_[9] = {}, result_[7] = {};
    int cmdLen_ = 0, cmdPos_ = 0, resultLeft_ = 0, resultPos_ = 0;
};

}  // namespace ens::ts
