// The TS-12 OS v3.10 on the machine with Model::Ts12 (DECISION T31): its 76 keys, the release byte, channel
// pressure from mux input Y1, and the battery RAM shared with the TS-10. Like test_ts_firmware.cpp, these look
// only at the panel and MIDI out.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "ts_firmware.h"
#include "ts_render.h"

using namespace ens::ts;

namespace {

std::unique_ptr<Machine> boot(Model model) {
    auto m = loadTs(false, false, false, model);
    if (m) runMs(*m, kBootMs);
    return m;
}

std::vector<uint8_t> midiOut(Machine& m) {
    std::vector<uint8_t> out;
    for (uint8_t b; m.popMidiOut(b);) out.push_back(b);
    return out;
}

bool contains(const std::vector<uint8_t>& v, std::initializer_list<uint8_t> seq) {
    return std::search(v.begin(), v.end(), seq.begin(), seq.end()) != v.end();
}

bool has(const std::vector<uint8_t>& v, uint8_t b) { return std::find(v.begin(), v.end(), b) != v.end(); }

// A key pressed for 30 ms, then released for 30 ms; what went out on MIDI meanwhile.
std::vector<uint8_t> strike(Machine& m, int note, int velocity, int release) {
    m.key(note, true, velocity);
    runMs(m, 30);
    m.key(note, false, release);
    runMs(m, 30);
    return midiOut(m);
}

bool playScreen(const Machine& m) { return rowText(m.panel(), 0).compare(0, 4, "U0-0") == 0; }

}  // namespace

// The TS-12 OS names itself during the boot and reaches the play screen (T31).
TEST(ts12_boots_to_play_screen) {
    auto m = loadTs(false, false, false, Model::Ts12);
    if (!m) return;
    CHECK(m->model() == Model::Ts12);
    bool named = false;
    for (uint32_t t = 0; t < kBootMs; t += 100) {
        runMs(*m, 100);
        named = named || shows(m->panel(), "TS-12");
    }
    std::printf("  |%s|\n", rowText(m->panel(), 0).c_str());
    CHECK(named);
    CHECK(playScreen(*m));
}

// Key codes 0-75 are notes 28-103, E1 to G7 (T31).
TEST(ts12_keys_span_e1_to_g7) {
    auto m = boot(Model::Ts12);
    if (!m) return;
    midiOut(*m);
    const auto low = strike(*m, 28, 99, 64), high = strike(*m, 103, 99, 64);
    const auto below = strike(*m, 27, 99, 64), above = strike(*m, 104, 99, 64);
    CHECK(contains(low, {0x90, 0x1C}));
    CHECK(contains(low, {0x80, 0x1C}));
    CHECK(contains(high, {0x90, 0x67}));
    CHECK(below.empty());
    CHECK(above.empty());
}

// The TS-12 OS turns the key-up byte into the note-off velocity (T31; the TS-10 sends 64, T17).
TEST(ts12_release_byte_sets_note_off_velocity) {
    auto m = boot(Model::Ts12);
    if (!m) return;
    midiOut(*m);
    const auto soft = strike(*m, 52, 99, 64), hard = strike(*m, 52, 99, 113);
    CHECK(contains(soft, {0x80, 0x34, 0x14}));
    CHECK(contains(hard, {0x80, 0x34, 0x6E}));
}

// Mux input Y1 is the TS-12's pressure: channel pressure, filtered by the OS (several messages, running status).
TEST(ts12_pressure_is_analog_y1) {
    auto m = boot(Model::Ts12);
    if (!m) return;
    midiOut(*m);
    m->setAnalog(Analog::Pressure, 0x280);
    runMs(*m, 300);
    const auto up = midiOut(*m);
    m->setAnalog(Analog::Pressure, 0);
    runMs(*m, 300);
    const auto down = midiOut(*m);
    std::printf("  %zu bytes up, %zu down\n", up.size(), down.size());
    CHECK(has(up, 0xD0));
    CHECK(!up.empty() && up.back() == 0x7F);
    CHECK(!down.empty() && down.back() == 0x00);
}

// The TS-10 OS never selects Y1: the input does nothing there.
TEST(ts10_ignores_pressure_input) {
    auto m = boot(Model::Ts10);
    if (!m) return;
    midiOut(*m);
    m->setAnalog(Analog::Pressure, 0x3FF);
    runMs(*m, 300);
    CHECK(!has(midiOut(*m), 0xD0));
}

// A battery RAM image written by one model's OS, booted by the other's (T31's Evidence).
TEST(ts12_and_ts10_memory_images_cross) {
    for (const Model from : {Model::Ts12, Model::Ts10}) {
        const Model to = from == Model::Ts12 ? Model::Ts10 : Model::Ts12;
        auto a = boot(from);
        if (!a) return;
        auto b = loadTs(false, false, false, to);
        if (!b) return;
        std::string err;
        CHECK(b->loadRamImage(a->ramImage(), err));
        runMs(*b, kBootMs);
        std::printf("  %s image on %s: |%s|\n  |%s|\n", from == Model::Ts12 ? "TS-12" : "TS-10",
                    to == Model::Ts12 ? "TS-12" : "TS-10", rowText(b->panel(), 0).c_str(),
                    rowText(b->panel(), 1).c_str());
        CHECK(playScreen(*b));
    }
}
