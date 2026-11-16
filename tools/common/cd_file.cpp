#include "cd_file.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <system_error>

using namespace ens::ts;
namespace fs = std::filesystem;

namespace {
bool endsWithNoCase(const std::string& s, const char* ext) {
    const std::string e = ext;
    return s.size() >= e.size() && sameNameNoCase(s.substr(s.size() - e.size()), e);
}
}  // namespace

bool CdFile::open(const std::string& path, std::string& err) {
    f_.close();
    f_.clear();
    sectors_ = 0;
    if (endsWithNoCase(path, ".iso")) {
        layout_ = isoLayout(path);
    } else if (endsWithNoCase(path, ".cue")) {
        std::ifstream c(fs::u8path(path), std::ios::binary);
        if (!c) { err = "cannot open " + path; return false; }
        const std::string text((std::istreambuf_iterator<char>(c)), std::istreambuf_iterator<char>());
        if (!parseCue(text, layout_, err)) return false;
        std::error_code ec;
        if (!fs::exists(fs::u8path(layout_.file), ec)) {
            // The .cue often names the ripper's own path; look for the name beside the .cue instead.
            const fs::path dir = fs::u8path(path).parent_path();
            const std::string want = baseName(layout_.file);
            std::string found;
            for (const auto& e : fs::directory_iterator(dir.empty() ? fs::path(".") : dir, ec))
                if (sameNameNoCase(e.path().filename().u8string(), want)) { found = e.path().u8string(); break; }
            if (found.empty()) { err = "image file not found: " + want; return false; }
            layout_.file = found;
        }
    } else {
        err = "unsupported CD image (use .iso or .cue)";
        return false;
    }
    std::error_code ec;
    const uint64_t bytes = fs::file_size(fs::u8path(layout_.file), ec);
    if (ec) { err = "cannot open " + layout_.file; return false; }
    if (!sectorCount(layout_, bytes, sectors_, err)) return false;
    f_.open(fs::u8path(layout_.file), std::ios::binary);
    if (!f_) { err = "cannot open " + layout_.file; sectors_ = 0; return false; }
    return true;
}

bool CdFile::read(uint32_t lba, uint8_t* out2048) {
    if (lba >= sectors_ || !f_.is_open()) return false;
    f_.clear();
    f_.seekg((std::streamoff)sectorOffset(layout_, lba));
    f_.read(reinterpret_cast<char*>(out2048), 2048);
    return (bool)f_;
}
