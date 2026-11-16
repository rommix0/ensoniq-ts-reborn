#pragma once
// CD image layouts: where a 2048-byte data sector sits in the image file. Pure arithmetic and text; the file
// itself is read by the tools (tools/common/cd_file.h) or the app. DECISION T29 (Plan 7): .iso, or a .cue with
// one MODE1/2352 track from 00:00:00.
#include <cstdint>
#include <string>

namespace ens::ts {

struct CdLayout {
    std::string file;      // image file named by the .cue (or the .iso path itself)
    uint32_t stride = 2048;  // bytes per sector in the file
    uint32_t skip = 0;       // bytes before the 2048 data bytes (MODE1/2352: 16 sync+header)
};

// First FILE, one TRACK 01 MODE1/2352 and INDEX 01 00:00:00 only; anything else fails with a message that
// names MODE1/2352.
bool parseCue(const std::string& text, CdLayout& out, std::string& err);
CdLayout isoLayout(const std::string& file);
inline uint64_t sectorOffset(const CdLayout& l, uint32_t lba) { return (uint64_t)lba * l.stride + l.skip; }
// Whole sectors in a file of fileBytes; fails when the size is not a multiple of the stride.
bool sectorCount(const CdLayout& l, uint64_t fileBytes, uint32_t& sectors, std::string& err);
std::string baseName(const std::string& path);  // after the last '/' or '\'
bool sameNameNoCase(const std::string& a, const std::string& b);

}  // namespace ens::ts
