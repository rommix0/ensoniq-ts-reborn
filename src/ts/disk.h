#pragma once
// A 3.5" floppy as the TS sees it: 80 cylinders, 2 heads, 512-byte sectors, 10 (DD) or 20 (HD) per track.
// DECISION T30: or more cylinders, up to 255, as an HxC floppy emulator serves (a "super disk").
// Allocates only in blank()/formatted(); everything else is allocation-free.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ens::ts {

enum class Density : uint8_t { Double, High };

class Disk {
public:
    static constexpr int kCylinders = 80, kMaxCylinders = 255, kHeads = 2, kSectorBytes = 512;

    // cylinders 1..kMaxCylinders (clamped).
    static std::unique_ptr<Disk> blank(Density d, int cylinders = kCylinders);      // every track unformatted
    static std::unique_ptr<Disk> formatted(Density d, int cylinders = kCylinders);  // every track formatted, zeros

    Density density() const { return density_; }
    int cylinders() const { return cylinders_; }
    int sectorsPerTrack() const { return density_ == Density::High ? 20 : 10; }
    size_t bytes() const { return data_.size(); }

    bool trackFormatted(int cyl, int head) const;
    void formatTrack(int cyl, int head, uint8_t fill);  // marks formatted, fills, counts a write

    // id 0..sectorsPerTrack-1; nullptr out of range or on an unformatted track.
    uint8_t* sector(int cyl, int head, int id);
    const uint8_t* sector(int cyl, int head, int id) const;
    // Ensoniq block order: n = (cyl * 2 + head) * sectorsPerTrack + id. Ignores the formatted flag.
    uint8_t* block(int n);
    const uint8_t* block(int n) const { return const_cast<Disk*>(this)->block(n); }

    void noteWrite() { ++writes_; }
    uint32_t writes() const { return writes_; }  // writes and formats, never reset
    bool writeProtected() const { return protect_; }
    void setWriteProtected(bool p) { protect_ = p; }

    // Blocks the OS has read (T-FDC Read Data, one per sector); the marks are sized with the disk so noteRead
    // never allocates. reads() counts blocks first marked; only clearReads resets it.
    void noteRead(int block) {
        if (block < 0 || size_t(block) >= read_.size() || read_[size_t(block)]) return;
        read_[size_t(block)] = 1;
        ++reads_;
    }
    bool wasRead(int block) const { return block >= 0 && size_t(block) < read_.size() && read_[size_t(block)] != 0; }
    uint32_t reads() const { return reads_; }
    void clearReads();

    // Contents, writes() and the read marks; false, nothing copied, when the densities or cylinders differ. The
    // write-protect flag is not copied: it belongs to the drive (A119), which sets it on each disk it puts in.
    bool copyInto(Disk& dst) const;

private:
    Disk(Density d, int cylinders);
    size_t trackOffset(int cyl, int head) const {
        return size_t(cyl * kHeads + head) * size_t(sectorsPerTrack()) * kSectorBytes;
    }
    Density density_;
    int cylinders_;
    std::vector<uint8_t> data_;
    std::vector<uint8_t> formatted_;  // per track (cyl * 2 + head)
    std::vector<uint8_t> read_;  // per block
    uint32_t writes_ = 0;
    uint32_t reads_ = 0;
    bool protect_ = false;
};

}  // namespace ens::ts
