#pragma once
// Host parameter position (0..1) <-> raw analog value (0..0x3FF). Nothing is inverted on the TS
// (docs/ts-hardware-interface.md, "Analog inputs"). Header-only; float is allowed here (the engine and
// the machine stay integer).
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "ts/machine.h"

namespace ens::app {

// DECISION A110: lround(position x 1023), clamped to 0..0x3FF; Data Entry to 1..0x3FF, because raw 0 makes the
// OS rewrite the selected value and show SYSTEM ERROR 144. The position may come from a project file: NaN gives
// the rest value, an infinity clamps.
inline uint16_t analogRaw(ts::Analog a, float position) {
    if (std::isnan(position)) return ts::kAnalogRest[static_cast<size_t>(a)];
    const float p = position < 0.0f ? 0.0f : position > 1.0f ? 1.0f : position;
    const auto raw = static_cast<uint16_t>(std::lround(p * 1023.0f));
    return a == ts::Analog::DataEntry && raw == 0 ? 1 : raw;
}

inline float analogPosition(ts::Analog, uint16_t raw) {
    return static_cast<float>(raw > 0x3FF ? 0x3FF : raw) / 1023.0f;
}

}  // namespace ens::app
