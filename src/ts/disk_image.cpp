#include "disk_image.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ens::ts {

namespace {

constexpr size_t kDdBytes = 819200, kHdBytes = 1638400;
constexpr size_t kGkhHeader = 58;
constexpr char kGkhMagic[] = "TDDFI";
// DECISION T21: the header of a new .gkh file is the real 58-byte one from the user's ED-001 (DD) and AD-001 (HD)
// files: "TDDFI", version 01, five 10-byte tags; tag 0A holds the geometry (80 cylinders, 2 heads, sectors per
// track at 0x18, 512-byte sectors), tag 0B the image size (LE32 at 0x1E) and offset (0x3A at 0x22). The two files
// differ only at 0x18 (0x0A / 0x14) and 0x1E..0x21 (0x000C8000 / 0x00190000).
constexpr uint8_t kGkhDd[kGkhHeader] = {
    'T', 'D', 'D', 'F', 'I', 0x01, 0x05, 0x00, 0x01, 0x04, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x0A, 0x05, 0x50, 0x00, 0x02, 0x00, 0x0A, 0x00, 0x00, 0x02, 0x0B, 0x0B, 0x00, 0x80,
    0x0C, 0x00, 0x3A, 0x00, 0x00, 0x00, 0x15, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x14, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
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
    if (f[0x4E] != 0x0D || f[0x4F] != 0x0A) {
        err = "not a Giebler file: the header text is missing";
        return false;
    }
    // The disk-type byte (0x1FF: 3 or 7 DD, 0xCB or 0xCC HD) picks the density to try first; the file size
    // (512 + 512 per stored block + 1) must agree with the bitmap, which settles unknown type bytes.
    const bool hdFirst = f[0x1FF] >= 0x80;
    for (int pass = 0; pass < 2; ++pass) {
        const Density d = (pass == 0) == hdFirst ? Density::High : Density::Double;
        size_t stored = 0;
        for (size_t n = 0; n < blockCount(d); ++n) stored += leftOut(f, d, n) ? 0 : 1;
        const size_t b = bitmapAt(d);
        // The marker 0D 0A 1A sits just before the bitmap, and the size must be exact (T21).
        if (f[b - 3] != 0x0D || f[b - 2] != 0x0A || f[b - 1] != kEof) continue;
        if (f.size() != kGiebHeader + stored * Disk::kSectorBytes + 1) continue;
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
    err = "damaged Giebler file: the size does not match the bitmap";
    return false;
}

// DECISION T30: HFE, the HxC floppy emulator's format, version 1 ("HXCPICFE") with ISO/IBM MFM tracks: a 512-byte
// header (track count at 9, sides at 10, encoding at 11, bit rate in kbit/s LE16 at 12, interface mode at 16, track
// list block LE16 at 18), a track list of {block LE16, length LE16} per cylinder, and each track's cells in 512-byte
// blocks, 256 bytes of side 0 then 256 of side 1, the first cell in bit 0. Each data bit is a clock cell and a data
// cell; a mark is A1 A1 A1 with a missing clock (cells 0x4489), then FE (ID: C H R N, CRC) or FB (data, CRC), the
// CRC-CCITT from 0xFFFF over A1 A1 A1, the mark and what follows.
constexpr char kHfeMagic[] = "HXCPICFE";
constexpr size_t kHfeBlock = 512;
constexpr uint16_t kSyncA1 = 0x4489, kSyncC2 = 0x5224;

uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc = uint16_t(crc ^ (p[i] << 8));
        for (int k = 0; k < 8; ++k) crc = uint16_t((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
uint16_t le16(const std::vector<uint8_t>& f, size_t at) { return uint16_t(f[at] | f[at + 1] << 8); }

// One side's cells, first cell first.
struct Cells {
    std::vector<uint8_t> bytes;
    size_t size() const { return bytes.size() * 8; }
    int at(size_t i) const { return (bytes[i >> 3] >> (i & 7)) & 1; }
    uint16_t raw16(size_t i) const {  // 16 cells from i, the first in bit 15
        uint16_t v = 0;
        for (size_t k = 0; k < 16; ++k) v = uint16_t(v << 1 | at(i + k));
        return v;
    }
    uint8_t byte(size_t i) const {  // the data cells of the 16 from i
        uint8_t v = 0;
        for (size_t k = 0; k < 8; ++k) v = uint8_t(v << 1 | at(i + 2 * k + 1));
        return v;
    }
};

// The sectors on one side into d; `any` set when the side has one. False on a bad sector (CRC or id).
bool decodeSide(const Cells& c, int cyl, int head, Disk& d, bool& any, std::string& err) {
    const size_t n = c.size();
    bool haveId = false;
    uint8_t id[4] = {};
    uint16_t sr = 0;
    for (size_t i = 0; i < n; ++i) {
        sr = uint16_t(sr << 1 | c.at(i));
        if (sr != kSyncA1) continue;
        size_t j = i + 1;  // after the first A1
        int syncs = 1;
        while (j + 16 <= n && c.raw16(j) == kSyncA1) {
            ++syncs;
            j += 16;
        }
        if (syncs < 3 || j + 16 > n) {
            i = j - 1;
            continue;
        }
        const uint8_t mark = c.byte(j);
        const size_t want = mark == 0xFE ? 6 : (mark == 0xFB ? 514 : 0);
        if (want == 0 || j + 16 * (1 + want) > n) {
            i = j + 15;
            continue;
        }
        uint8_t buf[4 + 514] = {0xA1, 0xA1, 0xA1, mark};
        for (size_t k = 0; k < want; ++k) buf[4 + k] = c.byte(j + 16 * (1 + k));
        const uint16_t got = uint16_t(buf[4 + want - 2] << 8 | buf[4 + want - 1]);
        const bool ok = crc16(buf, 4 + want - 2) == got;
        if (mark == 0xFE) {
            haveId = ok;
            std::memcpy(id, buf + 4, 4);
        } else if (haveId) {
            haveId = false;
            if (!ok || id[3] != 2 || id[2] >= d.sectorsPerTrack()) {
                err = "damaged HFE file: a bad sector at cylinder " + std::to_string(cyl) + ", head " +
                      std::to_string(head);
                return false;
            }
            if (!any) {
                d.formatTrack(cyl, head, 0);
                any = true;
            }
            std::memcpy(d.sector(cyl, head, id[2]), buf + 4, 512);
        }
        i = j + 16 * (1 + want) - 1;
    }
    return true;
}

bool decodeHfe(const std::vector<uint8_t>& f, DecodedImage& out, std::string& err) {
    if (f.size() < 2 * kHfeBlock) {
        err = "damaged HFE file: the header is cut short";
        return false;
    }
    const int tracks = f[9], sides = f[10];
    if (f[11] != 0 || sides < 1 || sides > 2 || tracks == 0) {
        err = "HFE file not read: only ISO/IBM MFM disks (HFE v1, track encoding 0) are supported";
        return false;
    }
    const size_t list = size_t(le16(f, 18)) * kHfeBlock;
    if (list + size_t(tracks) * 4 > f.size()) {
        err = "damaged HFE file: the track list is cut short";
        return false;
    }
    const Density den = le16(f, 12) >= 400 ? Density::High : Density::Double;
    auto d = Disk::blank(den, tracks);
    int last = -1;
    for (int t = 0; t < tracks; ++t) {
        const size_t at = size_t(le16(f, list + 4 * size_t(t))) * kHfeBlock;
        const size_t len = le16(f, list + 4 * size_t(t) + 2);
        const size_t end = at + (len / 2 + 255) / 256 * kHfeBlock;  // the last block holds both sides' tail
        if (at < 2 * kHfeBlock || end > f.size() + kHfeBlock) {
            err = "damaged HFE file: track " + std::to_string(t) + " is cut short";
            return false;
        }
        for (int s = 0; s < sides; ++s) {
            Cells c;
            c.bytes.reserve(len / 2);
            for (size_t i = 0; i < len / 2; ++i) {
                const size_t k = at + (i / 256) * kHfeBlock + size_t(s) * 256 + i % 256;
                c.bytes.push_back(k < f.size() ? f[k] : 0);
            }
            bool any = false;
            if (!decodeSide(c, t, s, *d, any, err)) return false;
            if (any) last = std::max(last, t);
        }
    }
    // Trailing blank tracks (84-track files with 80-83 empty) are not cylinders of the disk.
    const int cyls = std::max(Disk::kCylinders, last + 1);
    if (cyls == tracks) {
        out.disk = std::move(d);
    } else {
        out.disk = Disk::blank(den, cyls);
        for (int cy = 0; cy < std::min(cyls, tracks); ++cy)
            for (int h = 0; h < Disk::kHeads; ++h)
                if (d->trackFormatted(cy, h)) {
                    out.disk->formatTrack(cy, h, 0);
                    for (int r = 0; r < d->sectorsPerTrack(); ++r)
                        std::memcpy(out.disk->sector(cy, h, r), d->sector(cy, h, r), Disk::kSectorBytes);
                }
    }
    out.format = ImageFormat::Hfe;
    out.header.assign(f.begin(), f.begin() + kHfeBlock);
    return true;
}

// MFM cells, appended first cell first (bit 0 of each byte).
struct CellWriter {
    std::vector<uint8_t> bytes;
    size_t n = 0;
    int prev = 0;  // the last data bit
    void cell(int b) {
        if (n % 8 == 0) bytes.push_back(0);
        if (b) bytes.back() = uint8_t(bytes.back() | 1 << (n % 8));
        ++n;
    }
    void raw16(uint16_t v, int lastData) {
        for (int k = 15; k >= 0; --k) cell((v >> k) & 1);
        prev = lastData;
    }
    void byte(uint8_t v) {
        for (int k = 7; k >= 0; --k) {
            const int b = (v >> k) & 1;
            cell(!prev && !b);
            cell(b);
            prev = b;
        }
    }
    void fill(uint8_t v, int count) {
        for (int i = 0; i < count; ++i) byte(v);
    }
};

// DECISION T30: a track as a PC floppy controller formats it (gap 4a, IAM, gap 1; per sector sync, ID, gap 2, sync,
// data, gap 3) with gap 3 of 30 bytes (DD, 10 sectors) or 40 (HD, 20) so one revolution holds it: 6250 bytes at
// 250 kbit/s and 300 rpm, 12 500 at 500 kbit/s. An unformatted track is all 4E: no marks.
std::vector<uint8_t> encodeSide(const Disk& d, int cyl, int head, size_t trackBytes) {
    CellWriter w;
    w.bytes.reserve(2 * trackBytes);
    if (d.trackFormatted(cyl, head)) {
        const int spt = d.sectorsPerTrack();
        w.fill(0x4E, 80);
        w.fill(0x00, 12);
        for (int k = 0; k < 3; ++k) w.raw16(kSyncC2, 0);
        w.byte(0xFC);
        w.fill(0x4E, 50);
        for (int r = 0; r < spt; ++r) {
            uint8_t buf[4 + 512] = {0xA1, 0xA1, 0xA1, 0xFE, uint8_t(cyl), uint8_t(head), uint8_t(r), 2};
            w.fill(0x00, 12);
            for (int k = 0; k < 3; ++k) w.raw16(kSyncA1, 1);
            for (int k = 3; k < 8; ++k) w.byte(buf[k]);
            uint16_t crc = crc16(buf, 8);
            w.byte(uint8_t(crc >> 8));
            w.byte(uint8_t(crc));
            w.fill(0x4E, 22);
            w.fill(0x00, 12);
            for (int k = 0; k < 3; ++k) w.raw16(kSyncA1, 1);
            buf[3] = 0xFB;
            std::memcpy(buf + 4, d.sector(cyl, head, r), 512);
            for (size_t k = 3; k < sizeof buf; ++k) w.byte(buf[k]);
            crc = crc16(buf, sizeof buf);
            w.byte(uint8_t(crc >> 8));
            w.byte(uint8_t(crc));
            w.fill(0x4E, spt == 20 ? 40 : 30);
        }
    }
    while (w.bytes.size() < 2 * trackBytes) w.byte(0x4E);
    w.bytes.resize(2 * trackBytes);
    return w.bytes;
}

std::vector<uint8_t> encodeHfe(const Disk& d, const std::vector<uint8_t>& header) {
    const bool hd = d.density() == Density::High;
    const int cyls = d.cylinders();
    std::vector<uint8_t> out(2 * kHfeBlock, 0xFF);
    if (header.size() == kHfeBlock && std::memcmp(header.data(), kHfeMagic, 8) == 0) {
        std::copy(header.begin(), header.end(), out.begin());
    } else {
        std::memcpy(out.data(), kHfeMagic, 8);
        out[8] = 0;  // revision
        const uint16_t rate = hd ? 500 : 250;
        out[12] = uint8_t(rate);
        out[13] = uint8_t(rate >> 8);
        out[14] = out[15] = 0;  // rpm, unused (as the user's files)
        out[16] = hd ? 1 : 0;   // IBM PC HD / DD interface mode
        out[17] = 1;
    }
    out[9] = uint8_t(cyls);
    out[10] = 2;
    out[11] = 0;  // ISO/IBM MFM
    out[18] = 1;  // the track list in block 1
    out[19] = 0;
    const size_t trackBytes = hd ? 12500 : 6250;    // data bytes a side
    const size_t sideCells = 2 * trackBytes;        // cell bytes a side
    const size_t blocks = (sideCells + 255) / 256;  // 512-byte blocks a track
    out.reserve(out.size() + size_t(cyls) * blocks * kHfeBlock);
    for (int t = 0; t < cyls; ++t) {
        const size_t at = 2 + size_t(t) * blocks;
        const size_t e = kHfeBlock + 4 * size_t(t);
        out[e] = uint8_t(at);
        out[e + 1] = uint8_t(at >> 8);
        out[e + 2] = uint8_t(2 * sideCells);
        out[e + 3] = uint8_t((2 * sideCells) >> 8);
        std::vector<uint8_t> track(blocks * kHfeBlock, 0);
        for (int s = 0; s < Disk::kHeads; ++s) {
            const auto cells = encodeSide(d, t, s, trackBytes);
            for (size_t i = 0; i < cells.size(); ++i)
                track[(i / 256) * kHfeBlock + size_t(s) * 256 + i % 256] = cells[i];
        }
        out.insert(out.end(), track.begin(), track.end());
    }
    return out;
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
    if (file.size() >= 8 && std::memcmp(file.data(), kHfeMagic, 8) == 0) return decodeHfe(file, out, err);
    if (file.size() >= 8 && std::memcmp(file.data(), "HXCHFEV3", 8) == 0) {
        err = "HFE v3 files are not read: convert it to HFE v1 with the HxC software";
        return false;
    }
    err = "not a disk image: expected 819200 or 1638400 bytes, a .gkh, Giebler or .hfe file";
    return false;
}

std::vector<uint8_t> encodeImage(const Disk& d, ImageFormat f, const std::vector<uint8_t>& header) {
    if (f == ImageFormat::Hfe) return encodeHfe(d, header);
    if (d.cylinders() != Disk::kCylinders) return {};  // DECISION T30: Raw, GKH and Giebler hold 80 cylinders
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
                out.assign(kGkhDd, kGkhDd + kGkhHeader);
                if (d.density() == Density::High) {
                    out[0x18] = 0x14;
                    out[0x20] = 0x19;
                    out[0x21] = 0x00;
                    out[0x1E] = 0x00;
                    out[0x1F] = 0x00;
                }
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
    if (ext == "hfe") return ImageFormat::Hfe;
    return ImageFormat::Raw;
}

}  // namespace ens::ts
