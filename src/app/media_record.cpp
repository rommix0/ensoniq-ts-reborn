#include "app/media_record.h"

#include <algorithm>

namespace ens::app {

// DECISION A131: block 1, not block 2: on ED-001, CDR-13 and CDR-15 the label sits at bytes 31-37 of block 1
// (" ED-001", "CDR-013", "CDR-015"), and the OS prints those 7 bytes in its prompt ("NAME  ED-001").
std::string ensoniqLabel(const uint8_t* id) {
    if (id[38] != 'I' || id[39] != 'D') return "";
    std::string s(reinterpret_cast<const char*>(id + 31), 7);
    const auto keep = s.find_last_not_of(std::string(" \0", 2));
    if (keep == std::string::npos) return "";
    s.erase(keep + 1);
    return s.substr(s.find_first_not_of(' '));
}

// A floppy is known by its kind and label. An unnamed one (no ID block) is the last medium recorded when that is an
// unnamed floppy of the same kind, else a new one.
void MediaRecord::captureFloppy(const ts::Disk& copy) {
    const auto kind = copy.density() == ts::Density::High ? SavedMedium::Kind::FloppyHD : SavedMedium::Kind::FloppyDD;
    const std::string label = ensoniqLabel(copy.block(1));
    SavedMedium* m = nullptr;
    if (!label.empty()) {
        for (auto& s : media_)
            if (s.kind == kind && s.label == label) m = &s;
    } else if (!media_.empty() && media_.back().kind == kind && media_.back().label.empty()) {
        m = &media_.back();
    }
    const int n = int(copy.bytes() / ts::Disk::kSectorBytes);
    for (int b = 0; b < n; ++b) {
        if (!copy.wasRead(b) || (m && m->blocks.count(uint32_t(b)))) continue;
        if (!m) {  // only media the OS read are kept
            media_.push_back({kind, label, uint32_t(n), {}});
            m = &media_.back();
        }
        const uint8_t* p = copy.block(b);
        m->blocks.emplace(uint32_t(b), std::vector<uint8_t>(p, p + ts::Disk::kSectorBytes));
    }
}

void MediaRecord::captureCd(uint32_t size, uint32_t lba, const uint8_t* sector) {
    if (lba == 0) {  // sector 0 holds blocks 0-3, the ID block at 1: the volume now mounted
        const std::string label = ensoniqLabel(sector + ts::Disk::kSectorBytes);
        cd_ = -1;
        for (size_t i = 0; i < media_.size(); ++i)
            if (media_[i].kind == SavedMedium::Kind::Cd && media_[i].label == label && media_[i].size == size)
                cd_ = int(i);
        if (cd_ < 0) {
            media_.push_back({SavedMedium::Kind::Cd, label, size, {}});
            cd_ = int(media_.size() - 1);
        }
    }
    if (cd_ < 0) return;
    media_[size_t(cd_)].blocks.emplace(lba, std::vector<uint8_t>(sector, sector + 2048));  // first capture wins
}

void MediaRecord::clear() { setMedia({}); }

void MediaRecord::merge(const std::vector<SavedMedium>& under) {
    for (const auto& u : under) {
        const auto it = std::find_if(media_.begin(), media_.end(), [&](const SavedMedium& m) {
            return m.kind == u.kind && m.label == u.label && m.size == u.size;
        });
        if (it == media_.end())
            media_.push_back(u);  // appended: cd_ still names the same medium
        else
            it->blocks.insert(u.blocks.begin(), u.blocks.end());  // map::insert keeps the blocks already there
    }
}

void MediaRecord::setMedia(std::vector<SavedMedium> m) {
    media_ = std::move(m);
    cd_ = -1;
}

std::unique_ptr<ts::Disk> standInFloppy(const SavedMedium& m) {
    if (m.kind == SavedMedium::Kind::Cd) return nullptr;
    const bool hd = m.kind == SavedMedium::Kind::FloppyHD;
    // DECISION T30: the size in blocks gives the cylinders (80 but for an HFE super disk).
    auto d = ts::Disk::formatted(hd ? ts::Density::High : ts::Density::Double, int(m.size) / (hd ? 40 : 20));
    for (const auto& [b, bytes] : m.blocks)
        if (uint8_t* p = d->block(int(b)); p && bytes.size() == ts::Disk::kSectorBytes)
            std::copy(bytes.begin(), bytes.end(), p);
    return d;
}

}  // namespace ens::app
