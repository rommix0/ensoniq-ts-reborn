#include <cmath>
#include <cstdio>

#include "check.h"
#include "ts_firmware.h"

using namespace ens::ts;

static RomSet blank() {
    RomSet r;
    r.osHigh.assign(0x40000, 0);
    r.osLow.assign(0x80000, 0xFF);
    r.u41.assign(0x200000, 0);
    r.u43.assign(0x100000, 0);
    r.u45.assign(0x200000, 0);
    r.u46.assign(0x100000, 0);
    return r;
}

TEST(ts_os_image_interleaves_lower_half) {
    RomSet r = blank();
    r.osHigh[0] = 0x11;
    r.osHigh[1] = 0x22;
    r.osLow[0] = 0xA1;
    r.osLow[1] = 0xA2;
    std::vector<uint16_t> w;
    std::string err;
    CHECK(buildOsImage(r, w, err));
    CHECK_EQ(w.size(), 0x40000u);
    CHECK_EQ(w[0], 0x11A1);
    CHECK_EQ(w[1], 0x22A2);
    r.osLow.resize(0x40000);
    std::vector<uint16_t> w2;
    CHECK(buildOsImage(r, w2, err));
    CHECK(w == w2);
}

TEST(ts_bank0_builds_12_bit_samples) {
    RomSet r = blank();
    r.u41[5] = 0x12;
    r.u43[5] = 0x34;
    r.u41[0x100005] = 0x56;
    WaveMemory m;
    std::string err;
    CHECK(m.load(r, err));
    CHECK_EQ(m.read(0, 5), 0x1240);
    CHECK_EQ(m.read(0, 0x100005), 0x5630);
}

TEST(ts_bank1_regions) {
    RomSet r = blank();
    r.u45[2 * 7] = 0xAB;
    r.u45[2 * 7 + 1] = 0xCD;
    r.u46[2 * 9] = 0x12;
    r.u46[2 * 9 + 1] = 0x34;
    WaveMemory m;
    std::string err;
    CHECK(m.load(r, err));
    CHECK_EQ(m.read(1, 7), 0xABCD);
    CHECK_EQ(m.read(1, 0x100009), 0x1234);
    CHECK_EQ(m.read(1, 0x180000), 0);
    CHECK_EQ(m.read(1, 0x1FFFFF), 0);
}

// T4: the factory SIMM pair (BankSet S8, CPU 0x400000) is OTTO bank 3; bank 2 (S9's sockets) is empty.
TEST(ts_sample_ram_is_otto_bank3) {
    WaveMemory m;
    m.sampleRam(7) = 0xBEEF;
    CHECK_EQ(m.read(3, 7), 0xBEEF);
    CHECK_EQ(m.read(3, 0x100000), 0);
    CHECK_EQ(m.read(2, 7), 0);
}

// T4: the expansion pair (two 4M x 8 SIMMs, 8 MB): S8 is all of bank 3, S9 all of bank 2; the host index runs
// through bank 3, then bank 2 (CPU 0x400000, 0x800000).
TEST(ts_sample_ram_8mb_fills_banks_3_and_2) {
    WaveMemory m;
    m.setSampleRamWords(WaveMemory::kExpandedRamWords);
    CHECK_EQ(m.sampleRamWords(), 0x400000u);
    m.sampleRam(0x1FFFFF) = 0x1234;
    m.sampleRam(0x200007) = 0xBEEF;
    CHECK_EQ(m.read(3, 0x1FFFFF), 0x1234);
    CHECK_EQ(m.read(2, 7), 0xBEEF);
    CHECK_EQ(m.read(3, 7), 0);
}

TEST(ts_roms_wrong_size_named) {
    const char* names[6] = {"osHigh", "osLow", "u41", "u43", "u45", "u46"};
    for (int i = 0; i < 6; ++i) {
        RomSet r = blank();
        r.osLow.resize(0x40000);
        std::vector<uint8_t>* v[6] = {&r.osHigh, &r.osLow, &r.u41, &r.u43, &r.u45, &r.u46};
        v[i]->pop_back();
        std::string err;
        std::vector<uint16_t> w;
        WaveMemory m;
        const bool ok = i < 2 ? buildOsImage(r, w, err) : m.load(r, err);
        CHECK(!ok);
        CHECK(err.rfind(names[i], 0) == 0);
    }
}

// Mean |s[i+1]-s[i]| over 16-bit words, first byte high (order 0) or low (order 1).
static double roughness(const std::vector<uint8_t>& v, int order) {
    double sum = 0;
    const size_t n = v.size() / 2;
    int prev = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t a = v[2 * i], b = v[2 * i + 1];
        const int s = static_cast<int16_t>(order == 0 ? a << 8 | b : b << 8 | a);
        if (i) sum += std::abs(s - prev);
        prev = s;
    }
    return sum / static_cast<double>(n - 1);
}

TEST(ts_word_rom_byte_order) {
    RomSet r;
    if (!readTsRoms(r)) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    const std::vector<uint8_t>* roms[2] = {&r.u45, &r.u46};
    for (int k = 0; k < 2; ++k) {
        const double hi = roughness(*roms[k], 0), lo = roughness(*roms[k], 1);
        std::printf("  %s: first-byte-high mean %.1f, first-byte-low mean %.1f\n", k ? "u46" : "u45", hi, lo);
        CHECK(hi * 2 <= lo);  // the loader uses first byte high
    }
}
