#include "RomFolder.h"

namespace {

struct RomFile {
    const char* file;
    const char* what;
    juce::int64 size, alt;  // the sizes Machine::create takes (src/ts/roms.h); alt 0: one size only
    std::vector<uint8_t> ens::ts::RomSet::*member;
};

using RS = ens::ts::RomSet;
const RomFile kOs10[] = {
    {"os/ts10_310h.bin", "OS v3.10, high (256 KB)", 0x40000, 0, &RS::osHigh},
    {"os/ts10_310l.bin", "OS v3.10, low (512 KB)", 0x80000, 0x40000, &RS::osLow},
};
// DECISION A125: the TS-12's OS pair has the TS-10's sizes.
const RomFile kOs12[] = {
    {"os/ts12_310h.bin", "TS-12 OS v3.10, high (256 KB)", 0x40000, 0, &RS::osHigh},
    {"os/ts12_310l.bin", "TS-12 OS v3.10, low (512 KB)", 0x80000, 0x40000, &RS::osLow},
};
const RomFile kWaves[] = {
    {"waves/901401_u41.bin", "Wave ROM U41 (2 MB)", 0x200000, 0, &RS::u41},
    {"waves/901301_u43.bin", "Wave ROM U43 (1 MB)", 0x100000, 0, &RS::u43},
    {"waves/901402_u45.bin", "Wave ROM U45 (2 MB)", 0x200000, 0, &RS::u45},
    {"waves/901302_u46.bin", "Wave ROM U46 (1 MB)", 0x100000, 0, &RS::u46},
};


std::vector<RomFile> filesFor(ens::ts::Model model) {
    const auto* os = model == ens::ts::Model::Ts12 ? kOs12 : kOs10;
    std::vector<RomFile> v(os, os + 2);
    v.insert(v.end(), std::begin(kWaves), std::end(kWaves));
    return v;
}

}  // namespace

std::vector<RomFileStatus> checkRomFolder(const juce::File& dir, ens::ts::Model model, ens::ts::RomSet* outIfAllGood) {
    const auto kFiles = filesFor(model);
    std::vector<RomFileStatus> list;
    const bool isDir = dir.isDirectory();  // juce::File() would look in the drive's root
    bool good = true;
    for (const auto& f : kFiles) {
        const auto file = dir.getChildFile(f.file);
        const bool sizeOk = file.getSize() == f.size || (f.alt != 0 && file.getSize() == f.alt);
        const auto state = !isDir || !file.existsAsFile() ? RomFileStatus::Missing
                           : !sizeOk                        ? RomFileStatus::WrongSize
                                                            : RomFileStatus::Found;
        good = good && state == RomFileStatus::Found;
        list.push_back({f.file, f.what, state});
    }
    if (good && outIfAllGood != nullptr) {
        for (const auto& f : kFiles) {
            juce::MemoryBlock mb;
            dir.getChildFile(f.file).loadFileAsData(mb);
            const auto* p = static_cast<const uint8_t*>(mb.getData());
            (outIfAllGood->*f.member).assign(p, p + mb.getSize());
        }
    }
    return list;
}

namespace {
bool holdsRoms(const juce::File& dir, ens::ts::Model model) {
    if (!dir.isDirectory()) return false;
    for (const auto& s : checkRomFolder(dir, model, nullptr))
        if (s.state != RomFileStatus::Found) return false;
    return true;
}
}  // namespace

// DECISION A125: the stored folder (when it holds the ROMs for the model), "roms" beside the program (stored), the VST3 package's own ROMs (not
// stored: the package can be moved or removed), else the stored folder as it is, so its problem is shown.
RomFolderChoice findRomFolder(const juce::File& stored, const juce::File& program, ens::ts::Model model) {
    if (stored != juce::File() && holdsRoms(stored, model)) return {stored, false};
    const auto beside = program.getSiblingFile("roms");
    if (holdsRoms(beside, model)) return {beside, true};
    const auto contents = program.getParentDirectory().getParentDirectory();
    if (contents.getFileName() == "Contents") {
        const auto packaged = contents.getChildFile("Resources").getChildFile("roms");
        if (holdsRoms(packaged, model)) return {packaged, false};
    }
    return {stored, false};
}
