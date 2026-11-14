#pragma once
// Shared OTTO test helpers: a fake sound memory and 32-bit register access over the byte bus.
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "otto/otto.h"

struct FakeMem : ens::OttoMemory {
    uint16_t fill = 0;
    std::map<std::pair<int, uint32_t>, uint16_t> data;  // (bank, addr) -> word; else `fill`
    std::vector<std::pair<int, uint32_t>> reads;         // every (bank, addr) read, in order
    uint16_t read(int bank, uint32_t addr21) override {
        reads.emplace_back(bank, addr21);
        const auto it = data.find({bank, addr21});
        return it == data.end() ? fill : it->second;
    }
    bool sawRead(int bank, uint32_t addr) const {
        for (const auto& r : reads)
            if (r.first == bank && r.second == addr) return true;
        return false;
    }
};

inline void ottoSetPage(ens::Otto& o, uint8_t page) {
    for (int b = 0; b < 4; ++b) o.write(static_cast<uint8_t>(15 * 4 + b), b == 3 ? page : 0);
}

// Selects `page`, then writes `reg` bytes 0..3, most significant first.
inline void ottoWrite32(ens::Otto& o, uint8_t page, uint8_t reg, uint32_t v) {
    ottoSetPage(o, page);
    for (int b = 0; b < 4; ++b)
        o.write(static_cast<uint8_t>(reg * 4 + b), static_cast<uint8_t>(v >> (24 - 8 * b)));
}

// Selects `page`, then reads `reg` bytes 0..3 (byte 0 performs the 32-bit read).
inline uint32_t ottoRead32(ens::Otto& o, uint8_t page, uint8_t reg) {
    ottoSetPage(o, page);
    uint32_t v = 0;
    for (int b = 0; b < 4; ++b) v = (v << 8) | o.read(static_cast<uint8_t>(reg * 4 + b));
    return v;
}

// Register indexes (location / 8), by page group.
namespace otto_reg {
// Pages 00-1F
constexpr uint8_t CR = 0, FC = 1, LVOL = 2, LVRAMP = 3, RVOL = 4, RVRAMP = 5, ECOUNT = 6, K2 = 7,
                  K2RAMP = 8, K1 = 9, K1RAMP = 10, ACTV = 11, MODE = 12, PAR = 13, IRQV = 14, PAGE = 15;
// Pages 20-3F
constexpr uint8_t START = 1, END = 2, ACCUM = 3, O4_1 = 4, O3_1 = 5, O3_2 = 6, O2_1 = 7, O2_2 = 8,
                  O1_1 = 9, W_ST = 10, W_END = 11, LR_END = 12;
}  // namespace otto_reg
