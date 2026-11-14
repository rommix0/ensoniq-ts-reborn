#pragma once
// Helpers for the tests that use the TS ROM files from ENS_ROM_DIR.
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ts/roms.h"

// The TS-10 OS v3.10 and wave ROM files from ENS_ROM_DIR, unchecked; false when a file is missing.
inline bool readTsRoms(ens::ts::RomSet& r) {
    const std::string dir = ENS_ROM_DIR;
    const auto read = [](const std::string& path, std::vector<uint8_t>& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    };
    return read(dir + "/os/ts10_310h.bin", r.osHigh) && read(dir + "/os/ts10_310l.bin", r.osLow) &&
           read(dir + "/waves/901401_u41.bin", r.u41) && read(dir + "/waves/901301_u43.bin", r.u43) &&
           read(dir + "/waves/901402_u45.bin", r.u45) && read(dir + "/waves/901302_u46.bin", r.u46);
}
