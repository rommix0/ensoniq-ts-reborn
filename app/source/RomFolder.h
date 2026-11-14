#pragma once
// The ROM folder: the repo's roms/ layout, os/ and waves/, six files checked for presence and size.
#include <juce_core/juce_core.h>

#include <vector>

#include "ts/roms.h"

struct RomFileStatus {
    juce::String file, what;  // the path in the folder, and what it holds
    enum State { Found, Missing, WrongSize } state;
};

// One entry per file: the OS high and low, then the wave ROMs U41, U43, U45, U46. When every file is
// found with a size Machine::create takes and outIfAllGood is set, the files are read into it.
std::vector<RomFileStatus> checkRomFolder(const juce::File& dir, ens::ts::RomSet* outIfAllGood);
