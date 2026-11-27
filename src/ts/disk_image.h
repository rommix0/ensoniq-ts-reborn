#pragma once
// Disk image file codecs (design 2026-11-14-ts-floppy, section 6). Byte vectors in and out: file I/O
// belongs to the callers in tools/ and app/.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "disk.h"

namespace ens::ts {

enum class ImageFormat : uint8_t { Raw, Gkh, Giebler, Hfe };

struct DecodedImage {
    std::unique_ptr<Disk> disk;
    ImageFormat format = ImageFormat::Raw;
    std::vector<uint8_t> header;  // GKH, Giebler or HFE header as read, kept for saving
};

// Decides by content (size, "TDDFI", the 0D 0A signature, "HXCPICFE"), never by name.
bool decodeImage(const std::vector<uint8_t>& file, DecodedImage& out, std::string& err);
// An empty `header` makes a new one for that format and density. Unformatted tracks are written as zeros (HFE: as
// tracks with no sectors). Empty when the format cannot hold the disk: Raw, GKH and Giebler are 80 cylinders.
std::vector<uint8_t> encodeImage(const Disk& d, ImageFormat f, const std::vector<uint8_t>& header);
// By extension only (case-insensitive): .gkh Gkh; .ede .eda .edt Giebler; .hfe Hfe; anything else Raw.
ImageFormat formatForName(const std::string& fileName);

}  // namespace ens::ts
