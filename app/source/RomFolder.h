#pragma once
// The ROM folder: the repo's roms/ layout, os/ and waves/, the model's six files (four wave ROMs, its OS pair)
// checked for presence and size.
#include <juce_core/juce_core.h>

#include <vector>

#include "ts/panel.h"
#include "ts/roms.h"

struct RomFileStatus {
    juce::String file, what;  // the path in the folder, and what it holds
    enum State { Found, Missing, WrongSize } state;
};

// One entry per file: the model's OS high and low, then the wave ROMs U41, U43, U45, U46. When every file is
// found with a size Machine::create takes and outIfAllGood is set, the files are read into it.
std::vector<RomFileStatus> checkRomFolder(const juce::File& dir, ens::ts::Model model, ens::ts::RomSet* outIfAllGood);

struct RomFolderChoice {
    juce::File dir;      // empty: none found
    bool store = false;  // write it to settings.xml
};

// The folder the app starts on (A125): `stored` if it holds the ROMs for `model` (its OS pair and the four waves); else a "roms" folder beside
// `program`, stored; else, when `program` sits in a VST3 package (its parent's parent is named "Contents"),
// Contents/Resources/roms, not stored; else `stored` as it is (its problem is reported), not stored.
RomFolderChoice findRomFolder(const juce::File& stored, const juce::File& program, ens::ts::Model model);
