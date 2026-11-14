#pragma once
// Helpers for the tests that run the TS-10 OS v3.10 from ENS_ROM_DIR on the machine.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "ts/machine.h"

// The TS-10 OS v3.10 and wave ROM files from ENS_ROM_DIR, unchecked; false when a file is missing.
inline bool readTsRoms(ens::ts::RomSet& r) {
    const std::string dir = ENS_ROM_DIR;
    const auto read = [](const std::string& path, std::vector<uint8_t>& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    };
    return read(dir + "/os/ts10_310h.bin", r.osHigh) && read(dir + "/os/ts10_310l.bin", r.osLow) &&
           read(dir + "/waves/901401_u41.bin", r.u41) && read(dir + "/waves/901301_u43.bin", r.u43) &&
           read(dir + "/waves/901402_u45.bin", r.u45) && read(dir + "/waves/901302_u46.bin", r.u46);
}

// A fresh machine on the ROMs from ENS_ROM_DIR; nullptr when a file is missing (prints the skip).
// Files that exist but are rejected fail the running test, so a bad ROM set never passes as a skip.
inline std::unique_ptr<ens::ts::Machine> loadTs() {
    ens::ts::RomSet r;
    if (!readTsRoms(r)) {
        std::printf("  skip: ROMs not found\n");
        return nullptr;
    }
    std::string err;
    auto m = ens::ts::Machine::create(r, err);
    if (!m) {
        std::printf("  FAIL: ROMs present but rejected: %s\n", err.c_str());
        ++ens_test::failures();
    }
    return m;
}

// Runs whole frames until ms of CPU time (kCpuHz) have passed; the audio is discarded.
inline void runMs(ens::ts::Machine& m, uint32_t ms) {
    const uint64_t end = m.cycles() + static_cast<uint64_t>(ms) * ens::ts::kCpuHz / 1000;
    ens::ts::Audio a;
    while (m.cycles() < end) m.runFrame(a);
}

// FNV-1a 64 over every BusEvent field (each as 8 little-endian bytes); the machine's hook is restored.
struct TraceHasher {
    uint64_t h = 14695981039346656037ull;
    void mix(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    }
    void operator()(const ens::ts::BusEvent& e) {
        mix(e.cycle);
        mix(e.addr);
        mix(e.value);
        mix(e.size);
        mix(e.write);
        mix(static_cast<uint64_t>(e.dev));
    }
};

inline uint64_t traceHash(ens::ts::Machine& m, uint32_t ms) {
    TraceHasher t;
    auto saved = m.onBusEvent;
    m.onBusEvent = [&t](const ens::ts::BusEvent& e) { t(e); };
    runMs(m, ms);
    m.onBusEvent = saved;
    return t.h;
}
