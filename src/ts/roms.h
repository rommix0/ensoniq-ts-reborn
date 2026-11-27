#pragma once
// TS-10 ROM set: OS image and wave memory (Plan 3, Task 1). No file I/O here; the caller reads the files.
#include <cstdint>
#include <string>
#include <vector>

#include "otto/otto.h"

namespace ens::ts {

struct RomSet {                               // file images, unmodified
    std::vector<uint8_t> osHigh, osLow;       // 256 KB; osLow 256 KB or 512 KB
    std::vector<uint8_t> u41, u43, u45, u46;  // 2 MB, 1 MB, 2 MB, 1 MB
};

// 512K words (the 1 MB ROM window), word i = osHigh[i mod 256K] << 8 | osLow[i mod osLow size] (T2). err names the
// offending file.
bool buildOsImage(const RomSet&, std::vector<uint16_t>& words, std::string& err);

class WaveMemory : public OttoMemory {  // T3, T4
public:
    // DECISION T4: the SIMM pair fitted: two 1M x 8 (2 MB, the factory's) or two 4M x 8 (8 MB). Words index the
    // host window: bank 3 words 0-0x1FFFFF, then bank 2's.
    static constexpr uint32_t kSampleRamWords = 0x100000;
    static constexpr uint32_t kExpandedRamWords = 0x400000;
    bool load(const RomSet&, std::string& err);  // err names the file: "u45: expected 2097152 bytes, got ..."
    uint16_t read(int bank, uint32_t addr21) override;
    void setSampleRamWords(uint32_t words) { ram_.assign(words, 0); }  // kSampleRamWords or kExpandedRamWords
    uint32_t sampleRamWords() const { return static_cast<uint32_t>(ram_.size()); }
    // The CPU host window; the caller bounds the index by sampleRamWords() (T4).
    uint16_t& sampleRam(uint32_t word) { return ram_[word]; }
    uint16_t sampleRam(uint32_t word) const { return ram_[word]; }

private:
    std::vector<uint8_t> u41_, u43_, u45_, u46_;
    std::vector<uint16_t> ram_ = std::vector<uint16_t>(kSampleRamWords, 0);
};

}  // namespace ens::ts
