#pragma once
// Reads a CD image (.iso, or .cue + MODE1/2352 .bin) one 2048-byte sector at a time. File I/O lives here, not
// in src/. Read-only.
#include <cstdint>
#include <fstream>
#include <string>

#include "ts/cd_image.h"

class CdFile {
public:
    // Opens by extension (no case). A .cue's FILE that is missing falls back to the same name in the .cue's
    // folder, matched without case. On failure returns false with err set.
    bool open(const std::string& path, std::string& err);
    uint32_t sectors() const { return sectors_; }
    bool read(uint32_t lba, uint8_t* out2048);

private:
    std::ifstream f_;
    ens::ts::CdLayout layout_;
    uint32_t sectors_ = 0;
};
