#include "ts/roms.h"

namespace ens::ts {

static bool checkSize(const char* name, const std::vector<uint8_t>& v, size_t want, std::string& err) {
    if (v.size() == want) return true;
    err = std::string(name) + ": expected " + std::to_string(want) + " bytes, got " + std::to_string(v.size());
    return false;
}

bool buildOsImage(const RomSet& r, std::vector<uint16_t>& words, std::string& err) {
    if (!checkSize("osHigh", r.osHigh, 0x40000, err)) return false;
    if (r.osLow.size() != 0x40000 && !checkSize("osLow", r.osLow, 0x80000, err)) return false;
    // DECISION T2: h = even bytes, l = odd bytes from the first 256 KB of l (a 512 KB dump's upper half is unused).
    words.resize(0x40000);
    for (size_t i = 0; i < words.size(); ++i) words[i] = static_cast<uint16_t>(r.osHigh[i] << 8 | r.osLow[i]);
    return true;
}

bool WaveMemory::load(const RomSet& r, std::string& err) {
    if (!checkSize("u41", r.u41, 0x200000, err) || !checkSize("u43", r.u43, 0x100000, err) ||
        !checkSize("u45", r.u45, 0x200000, err) || !checkSize("u46", r.u46, 0x100000, err))
        return false;
    u41_ = r.u41;
    u43_ = r.u43;
    u45_ = r.u45;
    u46_ = r.u46;
    return true;
}

uint16_t WaveMemory::read(int bank, uint32_t a) {
    a &= 0x1FFFFF;
    switch (bank) {
    case 0: {
        // DECISION T3: bank 0 = U41 byte high, plus one U43 nibble at bits 7-4 (low nibble below 1M words, high above).
        if (u41_.empty()) return 0;
        const uint8_t b = u43_[a & 0xFFFFF];
        const unsigned nib = (a & 0x100000) ? b >> 4 : b & 0xF;
        return static_cast<uint16_t>(u41_[a] << 8 | nib << 4);
    }
    case 1: {
        // DECISION T3: bank 1 = U45 words below 1M, U46 words 1M..1.5M, then empty. Byte order by measurement:
        // the first byte of a word is the high byte.
        const std::vector<uint8_t>* v = a < 0x100000 ? &u45_ : (a < 0x180000 ? &u46_ : nullptr);
        if (!v || v->empty()) return 0;
        const uint32_t i = (a & 0xFFFFF) * 2;
        return static_cast<uint16_t>((*v)[i] << 8 | (*v)[i + 1]);
    }
    case 3:  // DECISION T4: BankSet S8 = bank 3 (1M words with 2 MB, 2M with 8 MB)
        return a < ram_.size() ? ram_[a] : 0;
    case 2:  // DECISION T4: BankSet S9 = bank 2, fitted only with 8 MB
        return 0x200000 + a < ram_.size() ? ram_[0x200000 + a] : 0;
    default:
        return 0;
    }
}

}  // namespace ens::ts
