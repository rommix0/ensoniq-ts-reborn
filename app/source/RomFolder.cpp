#include "RomFolder.h"

namespace {

struct RomFile {
    const char* file;
    const char* what;
    juce::int64 size, alt;  // the sizes Machine::create takes (src/ts/roms.h); alt 0: one size only
    std::vector<uint8_t> ens::ts::RomSet::*member;
};

using RS = ens::ts::RomSet;
const RomFile kFiles[] = {
    {"os/ts10_310h.bin", "OS v3.10, high (256 KB)", 0x40000, 0, &RS::osHigh},
    {"os/ts10_310l.bin", "OS v3.10, low (512 KB)", 0x80000, 0x40000, &RS::osLow},
    {"waves/901401_u41.bin", "Wave ROM U41 (2 MB)", 0x200000, 0, &RS::u41},
    {"waves/901301_u43.bin", "Wave ROM U43 (1 MB)", 0x100000, 0, &RS::u43},
    {"waves/901402_u45.bin", "Wave ROM U45 (2 MB)", 0x200000, 0, &RS::u45},
    {"waves/901302_u46.bin", "Wave ROM U46 (1 MB)", 0x100000, 0, &RS::u46},
};

}  // namespace

std::vector<RomFileStatus> checkRomFolder(const juce::File& dir, ens::ts::RomSet* outIfAllGood) {
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
