#include "ts/cd_image.h"

#include <cctype>
#include <sstream>

namespace ens::ts {

namespace {
const char* const kCueErr = "unsupported .cue: need one FILE, one TRACK 01 MODE1/2352 and INDEX 01 00:00:00";
std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}
}  // namespace

bool parseCue(const std::string& text, CdLayout& out, std::string& err) {
    std::istringstream in(text);
    std::string line, file;
    int tracks = 0;
    bool mode1 = false, index = false;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.compare(0, 5, "FILE ") == 0) {
            if (!file.empty()) continue;  // the first FILE only
            std::string r = trim(line.substr(5));
            if (!r.empty() && r[0] == '"') {
                size_t q = r.find('"', 1);
                file = r.substr(1, q == std::string::npos ? std::string::npos : q - 1);
            } else {
                size_t sp = r.rfind(' ');  // the type word (BINARY) is last
                file = trim(sp == std::string::npos ? r : r.substr(0, sp));
            }
        } else if (line.compare(0, 6, "TRACK ") == 0) {
            ++tracks;
            mode1 = line.size() >= 8 && trim(line.substr(6)) == "01 MODE1/2352";
        } else if (line.compare(0, 6, "INDEX ") == 0) {
            const std::string r = trim(line.substr(6));
            if (r.compare(0, 3, "01 ") == 0) {
                if (trim(r.substr(3)) != "00:00:00") { err = kCueErr; return false; }
                index = true;
            }
        }
    }
    if (file.empty() || tracks != 1 || !mode1 || !index) { err = kCueErr; return false; }
    out.file = file;
    out.stride = 2352;
    out.skip = 16;
    return true;
}

CdLayout isoLayout(const std::string& file) {
    CdLayout l;
    l.file = file;
    return l;
}

bool sectorCount(const CdLayout& l, uint64_t fileBytes, uint32_t& sectors, std::string& err) {
    if (fileBytes == 0 || fileBytes % l.stride != 0 || fileBytes / l.stride > 0xFFFFFFFFull) {
        err = "image size is not a whole number of " + std::to_string(l.stride) + "-byte sectors";
        return false;
    }
    sectors = (uint32_t)(fileBytes / l.stride);
    return true;
}

std::string baseName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

bool sameNameNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

}  // namespace ens::ts
