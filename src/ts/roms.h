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

// 256K words, word i = osHigh[i] << 8 | osLow[i] (T2). err names the offending file.
bool buildOsImage(const RomSet&, std::vector<uint16_t>& words, std::string& err);

class WaveMemory : public OttoMemory {  // T3, T4
public:
    static constexpr uint32_t kSampleRamWords = 0x100000;
    bool load(const RomSet&, std::string& err);  // err names the file: "u45: expected 2097152 bytes, got ..."
    uint16_t read(int bank, uint32_t addr21) override;
    uint16_t& sampleRam(uint32_t word) { return ram_[word]; }  // the CPU host window (Task 4 maps it)

private:
    std::vector<uint8_t> u41_, u43_, u45_, u46_;
    std::vector<uint16_t> ram_ = std::vector<uint16_t>(kSampleRamWords, 0);
};

}  // namespace ens::ts
