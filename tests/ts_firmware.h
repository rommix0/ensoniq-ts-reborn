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
#include "ts_render.h"  // runMs

// The OS v3.10 of the model (TS-10 or TS-12, T31) and the wave ROM files from ENS_ROM_DIR, unchecked; false when
// a file is missing.
inline bool readTsRoms(ens::ts::RomSet& r, ens::ts::Model model = ens::ts::Model::Ts10) {
    const std::string dir = ENS_ROM_DIR;
    const auto read = [](const std::string& path, std::vector<uint8_t>& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    };
    const std::string os = dir + (model == ens::ts::Model::Ts12 ? "/os/ts12_310" : "/os/ts10_310");
    return read(os + "h.bin", r.osHigh) && read(os + "l.bin", r.osLow) &&
           read(dir + "/waves/901401_u41.bin", r.u41) && read(dir + "/waves/901301_u43.bin", r.u43) &&
           read(dir + "/waves/901402_u45.bin", r.u45) && read(dir + "/waves/901302_u46.bin", r.u46);
}

// A fresh machine on the ROMs from ENS_ROM_DIR (a stock TS-10, or with the SQX-70 fitted: T13, or the SP-4 SCSI
// board: T25, or 8 MB of sample RAM: T4; a TS-12 on its own OS: T31); nullptr when a file is missing (prints the skip). Files that exist but are
// rejected fail the running test, so a bad ROM set never passes as a skip.
inline std::unique_ptr<ens::ts::Machine> loadTs(bool sqx70 = false, bool scsi = false, bool simm8 = false,
                                                ens::ts::Model model = ens::ts::Model::Ts10) {
    ens::ts::RomSet r;
    if (!readTsRoms(r, model)) {
        std::printf("  skip: ROMs not found\n");
        return nullptr;
    }
    std::string err;
    auto m = ens::ts::Machine::create(r, err, sqx70, scsi, simm8, model);
    if (!m) {
        std::printf("  FAIL: ROMs present but rejected: %s\n", err.c_str());
        ++ens_test::failures();
    }
    return m;
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

// A button pressed for 60 ms, then 300 ms for the OS to answer.
inline void tap(ens::ts::Machine& m, ens::ts::Button b) {
    m.button(b, true);
    runMs(m, 60);
    m.button(b, false);
    runMs(m, 300);
}

// The text on either display row.
inline bool shows(const ens::ts::PanelState& p, const char* text) {
    using ens::ts::rowText;
    return rowText(p, 0).find(text) != std::string::npos || rowText(p, 1).find(text) != std::string::npos;
}

// A voice started from sample RAM: a CR write (pages 0x00-0x3F, register 0) with BS1:BS0 = 3, the bank the OS
// gives BankSet S8 (T4). Follows the page register (15) and CR's byte 2 (bits 15-8); byte 3 completes the write.
struct SampleVoices {
    int count = 0;
    unsigned page = 0, bs = 0;
    void operator()(const ens::ts::BusEvent& e) {
        if (e.dev != ens::ts::Device::Otto || !e.write) return;
        const unsigned ha = (e.addr >> 1) & 0x3F, reg = ha >> 2, byte = ha & 3;
        if (reg == 15 && byte == 3) page = e.value & 0x7F;
        if (reg != 0 || page >= 0x40) return;
        if (byte == 2) bs = (e.value >> 6) & 3;
        if (byte == 3 && bs == 3) ++count;
    }
};
