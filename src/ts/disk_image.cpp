#include "disk_image.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ens::ts {

namespace {

constexpr size_t kDdBytes = 819200, kHdBytes = 1638400;
constexpr size_t kGkhHeader = 58;
constexpr char kGkhMagic[] = "TDDFI";
constexpr size_t kGiebHeader = 512;
constexpr uint8_t kEof = 0x1A;

// DECISION T21: Giebler layout from the four blank files. 512-byte header (0D 0A + title, 0D 0A at 0x4E,
// spaces, 0D 0A 1A just before the bitmap), a bitmap with one bit per block, MSB first, 1 = left out
// (all zero), at 0xA0 for DD (200 bytes) and at 0x60 for HD (400 bytes), a disk-type byte at 0x1FF, then
// the stored blocks in block order (512 bytes each) and one trailing 0x1A.
size_t bitmapAt(Density d) { return d == Density::High ? 0x60 : 0xA0; }
size_t blockCount(Density d) { return d == Density::High ? 3200 : 1600; }
bool leftOut(const std::vector<uint8_t>& h, Density d, size_t n) {
    return (h[bitmapAt(d) + n / 8] & (0x80 >> (n % 8))) != 0;
}

bool decodeGiebler(const std::vector<uint8_t>& f, DecodedImage& out, std::string& err) {
    if (f.size() < kGiebHeader + 1) {
        err = "damaged Giebler file: the 512-byte header is cut short";
        return false;
    }
    // The disk-type byte (0x1FF: 3 or 7 DD, 0xCB or 0xCC HD) picks the density to try first; the file size
    // (512 + 512 per stored block + 1) must agree with the bitmap, which settles unknown type bytes.
    const bool hdFirst = f[0x1FF] >= 0x80;
    for (int pass = 0; pass < 2; ++pass) {
        const Density d = (pass == 0) == hdFirst ? Density::High : Density::Double;
        size_t stored = 0;
        for (size_t n = 0; n < blockCount(d); ++n) stored += leftOut(f, d, n) ? 0 : 1;
        if (f.size() < kGiebHeader + stored * Disk::kSectorBytes + 1) continue;
        out.disk = Disk::formatted(d);
        out.format = ImageFormat::Giebler;
        out.header.assign(f.begin(), f.begin() + kGiebHeader);
        size_t at = kGiebHeader;
        for (size_t n = 0; n < blockCount(d); ++n) {
            if (leftOut(f, d, n)) continue;
            std::copy_n(f.begin() + std::ptrdiff_t(at), Disk::kSectorBytes, out.disk->block(int(n)));
            at += Disk::kSectorBytes;
        }
        return true;
    }
    err = "damaged Giebler file: the bitmap lists more blocks than the file holds";
    return false;
}

}  // namespace

bool decodeImage(const std::vector<uint8_t>& file, DecodedImage& out, std::string& err) {
    out = DecodedImage();
    if (file.size() == kDdBytes || file.size() == kHdBytes) {
        out.disk = Disk::formatted(file.size() == kHdBytes ? Density::High : Density::Double);
        std::copy(file.begin(), file.end(), out.disk->block(0));
        return true;
    }
    if ((file.size() == kGkhHeader + kDdBytes || file.size() == kGkhHeader + kHdBytes) &&
        std::memcmp(file.data(), kGkhMagic, 5) == 0) {
        const bool hd = file.size() == kGkhHeader + kHdBytes;
        out.disk = Disk::formatted(hd ? Density::High : Density::Double);
        out.format = ImageFormat::Gkh;
        out.header.assign(file.begin(), file.begin() + kGkhHeader);
        std::copy(file.begin() + kGkhHeader, file.end(), out.disk->block(0));
        return true;
    }
    if (file.size() >= 2 && file[0] == 0x0D && file[1] == 0x0A) return decodeGiebler(file, out, err);
    err = "not a disk image: expected 819200 or 1638400 bytes, a .gkh file or a Giebler file";
    return false;
}

std::vector<uint8_t> encodeImage(const Disk& d, ImageFormat f, const std::vector<uint8_t>& header) {
    const int spt = d.sectorsPerTrack();
    const int blocks = int(blockCount(d.density()));
    const std::vector<uint8_t> zero(Disk::kSectorBytes, 0);
    // Block n of the image; zeros when its track is unformatted.
    auto blockData = [&](int n) {
        const uint8_t* p = d.sector(n / (2 * spt), (n / spt) % 2, n % spt);
        return p ? p : zero.data();
    };
    std::vector<uint8_t> out;
    if (f != ImageFormat::Giebler) {
        if (f == ImageFormat::Gkh) {
            out = header;
            if (out.empty()) {
                out.assign(kGkhHeader, 0);
                std::memcpy(out.data(), kGkhMagic, 5);
            }
        }
        for (int n = 0; n < blocks; ++n) out.insert(out.end(), blockData(n), blockData(n) + Disk::kSectorBytes);
        return out;
    }
    const Density den = d.density();
    const size_t b = bitmapAt(den);
    const bool fresh = header.size() != kGiebHeader;
    std::vector<uint8_t> h = header;
    if (fresh) {
        h.assign(kGiebHeader, ' ');
        const char* title = den == Density::High ? "TS-10 (HD) Disk" : "TS-10 (DD) Disk";
        h[0] = 0x0D;
        h[1] = 0x0A;
        std::memcpy(&h[2], title, std::strlen(title));
        h[0x4E] = 0x0D;
        h[0x4F] = 0x0A;
        h[b - 3] = 0x0D;
        h[b - 2] = 0x0A;
        h[b - 1] = kEof;
        std::fill(h.begin() + std::ptrdiff_t(b), h.end(), uint8_t(0));
        h[0x1FF] = den == Density::High ? 0xCC : 0x07;
    }
    // A block is stored when it holds data, or when the header we were given already stored it, so a
    // decoded file written back unchanged is byte-identical even where the tool kept a zero block.
    std::vector<uint8_t> body;
    std::fill(h.begin() + std::ptrdiff_t(b), h.begin() + std::ptrdiff_t(b + size_t(blocks) / 8), uint8_t(0));
    for (int n = 0; n < blocks; ++n) {
        const uint8_t* p = blockData(n);
        const bool zeros = std::all_of(p, p + Disk::kSectorBytes, [](uint8_t v) { return v == 0; });
        const bool wasStored = !fresh && !leftOut(header, den, size_t(n));
        if (zeros && !wasStored) {
            h[b + size_t(n) / 8] |= uint8_t(0x80 >> (n % 8));
            continue;
        }
        body.insert(body.end(), p, p + Disk::kSectorBytes);
    }
    out = h;
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(kEof);
    return out;
}

ImageFormat formatForName(const std::string& fileName) {
    const size_t dot = fileName.rfind('.');
    if (dot == std::string::npos) return ImageFormat::Raw;
    std::string ext = fileName.substr(dot + 1);
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "gkh") return ImageFormat::Gkh;
    if (ext == "ede" || ext == "eda" || ext == "edt") return ImageFormat::Giebler;
    return ImageFormat::Raw;
}

}  // namespace ens::ts
