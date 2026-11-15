#pragma once
// A 3.5" floppy as the TS sees it: 80 cylinders, 2 heads, 512-byte sectors, 10 (DD) or 20 (HD) per track.
// Allocates only in blank()/formatted(); everything else is allocation-free.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ens::ts {

enum class Density : uint8_t { Double, High };

class Disk {
public:
    static constexpr int kCylinders = 80, kHeads = 2, kSectorBytes = 512;

    static std::unique_ptr<Disk> blank(Density d);      // every track unformatted
    static std::unique_ptr<Disk> formatted(Density d);  // every track formatted, zero-filled

    Density density() const { return density_; }
    int sectorsPerTrack() const { return density_ == Density::High ? 20 : 10; }
    size_t bytes() const { return data_.size(); }

    bool trackFormatted(int cyl, int head) const;
    void formatTrack(int cyl, int head, uint8_t fill);  // marks formatted, fills, counts a write

    // id 0..sectorsPerTrack-1; nullptr out of range or on an unformatted track.
    uint8_t* sector(int cyl, int head, int id);
    const uint8_t* sector(int cyl, int head, int id) const;
    // Ensoniq block order: n = (cyl * 2 + head) * sectorsPerTrack + id. Ignores the formatted flag.
    uint8_t* block(int n);

    void noteWrite() { ++writes_; }
    uint32_t writes() const { return writes_; }  // writes and formats, never reset
    bool writeProtected() const { return protect_; }
    void setWriteProtected(bool p) { protect_ = p; }

    bool copyInto(Disk& dst) const;  // false, nothing copied, when the densities differ

private:
    explicit Disk(Density d);
    size_t trackOffset(int cyl, int head) const {
        return size_t(cyl * kHeads + head) * size_t(sectorsPerTrack()) * kSectorBytes;
    }
    Density density_;
    std::vector<uint8_t> data_;
    std::vector<uint8_t> formatted_;  // per track (cyl * 2 + head)
    uint32_t writes_ = 0;
    bool protect_ = false;
};

}  // namespace ens::ts
