#include "disk.h"

#include <algorithm>

namespace ens::ts {

Disk::Disk(Density d)
    : density_(d),
      data_(size_t(kCylinders) * kHeads * size_t(d == Density::High ? 20 : 10) * kSectorBytes, 0),
      formatted_(size_t(kCylinders) * kHeads, 0) {}

std::unique_ptr<Disk> Disk::blank(Density d) { return std::unique_ptr<Disk>(new Disk(d)); }

std::unique_ptr<Disk> Disk::formatted(Density d) {
    auto disk = blank(d);
    std::fill(disk->formatted_.begin(), disk->formatted_.end(), uint8_t(1));
    return disk;
}

bool Disk::trackFormatted(int cyl, int head) const {
    if (cyl < 0 || cyl >= kCylinders || head < 0 || head >= kHeads) return false;
    return formatted_[size_t(cyl * kHeads + head)] != 0;
}

void Disk::formatTrack(int cyl, int head, uint8_t fill) {
    if (cyl < 0 || cyl >= kCylinders || head < 0 || head >= kHeads) return;
    formatted_[size_t(cyl * kHeads + head)] = 1;
    uint8_t* p = data_.data() + trackOffset(cyl, head);
    std::fill(p, p + size_t(sectorsPerTrack()) * kSectorBytes, fill);
    ++writes_;
}

uint8_t* Disk::sector(int cyl, int head, int id) {
    if (!trackFormatted(cyl, head) || id < 0 || id >= sectorsPerTrack()) return nullptr;
    return data_.data() + trackOffset(cyl, head) + size_t(id) * kSectorBytes;
}

const uint8_t* Disk::sector(int cyl, int head, int id) const {
    return const_cast<Disk*>(this)->sector(cyl, head, id);
}

uint8_t* Disk::block(int n) {
    if (n < 0 || size_t(n) * kSectorBytes >= data_.size()) return nullptr;
    return data_.data() + size_t(n) * kSectorBytes;
}

bool Disk::copyInto(Disk& dst) const {
    if (dst.density_ != density_) return false;
    std::copy(data_.begin(), data_.end(), dst.data_.begin());
    std::copy(formatted_.begin(), formatted_.end(), dst.formatted_.begin());
    dst.writes_ = writes_;  // DECISION A119: the app's "modified" baseline is the copy's count
    return true;
}

}  // namespace ens::ts
