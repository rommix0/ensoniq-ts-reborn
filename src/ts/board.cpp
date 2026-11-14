#include "ts/board.h"

namespace ens::ts {

// DECISION T11: 20-bit OTTO values enter the 16-bit ESP ports as sat16(v >> 4).
static int16_t sat16(int32_t v) { return static_cast<int16_t>(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
static int16_t to16(int32_t v20) { return sat16(v20 >> 4); }

Audio Board::route(const OttoFrame& f, int espSlots) {
    // DECISION T11: OTTO ch0-2 -> ESP SER0, SER2, SER3; ch3 -> aux; ch4/5 dropped; main = ESP SER1.
    static constexpr int kPort[3] = {0, 2, 3};
    for (int i = 0; i < 3; ++i) esp_.setSerialIn(kPort[i], to16(f.ch[i][0]), to16(f.ch[i][1]));
    if (espSlots > 0) esp_.advance(espSlots);
    Audio a;
    esp_.serialOut(1, a.mainL, a.mainR);
    a.auxL = to16(f.ch[3][0]);
    a.auxR = to16(f.ch[3][1]);
    return a;
}

}  // namespace ens::ts
