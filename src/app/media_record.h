#pragma once
// What a Project state keeps of the media the OS loaded from (Plan 7, A131): per floppy or CD, its kind, the label
// the OS knows it by and the blocks the OS read, kept from their first capture. A restore builds stand-ins from them.
// JUCE-free, no file I/O; not thread-safe: the app calls it on the message thread (CdDrive::poll hands it the sectors
// the reader thread served), under TsProcessor's media lock so a host may save from another thread (A129).
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ts/disk.h"

namespace ens::app {

struct SavedMedium {
    enum class Kind : uint8_t { FloppyDD, FloppyHD, Cd };
    Kind kind = Kind::FloppyDD;
    std::string label;   // the name the OS shows for it (from the ID block); "" when unnamed
    uint32_t size = 0;   // floppy: 1600 or 3200 blocks; CD: sectors
    std::map<uint32_t, std::vector<uint8_t>> blocks;  // floppy: 512-byte blocks; CD: 2048-byte sectors
};

// DECISION A131: the label of an Ensoniq disk or volume from its Device ID block (512-byte block 1): the 7 bytes at
// 31-37, after 0xFF at 30 and before "ID" at 38-39, spaces and NULs trimmed; "" without the "ID".
std::string ensoniqLabel(const uint8_t* idBlock);

class MediaRecord {
public:
    void captureFloppy(const ts::Disk& copy);                            // keeps newly read blocks, first capture wins
    void captureCd(uint32_t size, uint32_t lba, const uint8_t* sector);  // a sector served to the OS
    void clear();
    const std::vector<SavedMedium>& media() const { return media_; }
    void setMedia(std::vector<SavedMedium> m);

private:
    std::vector<SavedMedium> media_;
    int cd_ = -1;  // the current CD medium: the one whose sector 0 was captured last
};

// A formatted disk of the medium's density with the saved blocks in and zeros elsewhere, no reads marked; nullptr
// for a CD.
std::unique_ptr<ts::Disk> standInFloppy(const SavedMedium& m);

}  // namespace ens::app
