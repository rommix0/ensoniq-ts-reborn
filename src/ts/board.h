#pragma once
// TS-10 board: the OTTO, the ESP and the ESP delay DRAM (64K x 16), wired as DECISION T11.
// No file I/O, no OS calls; route() does not allocate.
#include <cstdint>
#include <vector>

#include "esp/esp.h"
#include "otto/otto.h"
#include "ts/roms.h"

namespace ens::ts {

constexpr int kEspSlotsPerFrame = 103;  // T11
constexpr uint32_t kEspDramWords = 65536;

struct Audio {
    int16_t mainL, mainR, auxL, auxR;
};

class Board : public EspBus {
public:
    explicit Board(WaveMemory& waves) : otto_(waves), esp_(*this), dram_(kEspDramWords, 0) {}
    Board(const Board&) = delete;  // otto_ and esp_ hold references
    Board& operator=(const Board&) = delete;

    Otto& otto() { return otto_; }
    Esp& esp() { return esp_; }

    // One frame of routing; espSlots = 0 while the ESP is halted.
    Audio route(const OttoFrame& f, int espSlots);

    // EspBus: word addresses wrap at 64K words; I/O space reads 0 and ignores writes.
    uint16_t read(bool io, uint32_t a) override { return io ? 0 : dram_[a & (kEspDramWords - 1)]; }
    void write(bool io, uint32_t a, uint16_t v) override {
        if (!io) dram_[a & (kEspDramWords - 1)] = v;
    }

private:
    Otto otto_;
    Esp esp_;
    std::vector<uint16_t> dram_;
};

}  // namespace ens::ts
