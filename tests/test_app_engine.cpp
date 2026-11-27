// The JUCE-free engine (Plan 4, Task 2, from KS-32 Reborn's): the machine through the resampler at the
// host rate, MIDI with block offsets (A101), GUI events (A102), the panel snapshot (A103, A104), the RAM
// hand-off, the current and requested sound (A105), analogs (A106) and the note mask (A107).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/resampler.h"
#include "app/spsc_ring.h"
#include "check.h"
#include "ts_firmware.h"

using namespace ens;
using ens::app::Engine;
using ens::app::Event;

namespace {

constexpr int kBlock = 480;  // 10 ms at 48 000 Hz
constexpr int kBootedMs = 5400;  // past kBootMs (5300) in 10 ms blocks

std::unique_ptr<Engine> makeEngine(bool scsi = false) {
    ts::RomSet roms;
    if (!readTsRoms(roms)) {
        std::printf("  skip: ROMs not found\n");
        return nullptr;
    }
    std::string err;
    auto e = Engine::create(roms, err, scsi);
    if (!e) {
        std::printf("  FAIL: ROMs present but rejected: %s\n", err.c_str());
        ++ens_test::failures();
        return nullptr;
    }
    e->prepare(48000, kBlock);
    return e;
}

// Renders ms of 10 ms blocks; Main left is appended to out when given.
void renderMs(Engine& e, int ms, std::vector<float>* out = nullptr) {
    float ml[kBlock], mr[kBlock], al[kBlock], ar[kBlock];
    for (int b = 0; b < ms / 10; ++b) {
        e.render(ml, mr, al, ar, kBlock);
        if (out) out->insert(out->end(), ml, ml + kBlock);
    }
}

std::unique_ptr<Engine> bootedEngine() {
    auto e = makeEngine();
    if (e) renderMs(*e, kBootedMs);
    return e;
}

std::string shown(const ts::PanelState& p) { return rowText(p, 0) + rowText(p, 1); }

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

int firstNonZero(const std::vector<float>& v) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] != 0.0f) return static_cast<int>(i);
    return -1;
}

Event button(ts::Button b, bool down) { return {Event::Kind::Button, static_cast<uint8_t>(b), down, 0}; }

void press(Engine& e, ts::Button b) {  // 100 ms held, 300 ms apart, as KS-32's tests press
    e.post(button(b, true));
    renderMs(e, 100);
    e.post(button(b, false));
    renderMs(e, 200);
}

}  // namespace

TEST(spsc_ring_fifo_and_full) {
    app::SpscRing<int, 4> q;
    for (int i = 0; i < 4; ++i) CHECK(q.push(i));
    CHECK(!q.push(4));
    for (int i = 0; i < 4; ++i) {
        int v = -1;
        CHECK(q.pop(v));
        CHECK_EQ(v, i);
    }
    int v;
    CHECK(!q.pop(v));
}

TEST(engine_create_rejects_wrong_rom_size) {
    ts::RomSet roms;
    if (!readTsRoms(roms)) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    roms.osHigh.pop_back();
    std::string err;
    CHECK(Engine::create(roms, err) == nullptr);
    CHECK(err.find("osHigh") != std::string::npos);
}

TEST(engine_matches_machine_and_resampler) {
    auto e = makeEngine();
    if (!e) return;
    auto m = loadTs();
    if (!m) return;
    app::Resampler rs(app::kFrameHz, 48000, 4);
    std::vector<float> ref;
    ref.reserve(4 * 48100);
    while (ref.size() < 4 * 48000) {
        ts::Audio a;
        m->runFrame(a);
        const int16_t s[4] = {a.mainL, a.mainR, a.auxL, a.auxR};
        rs.process(s, 1, ref);
    }
    float ml[kBlock], mr[kBlock], al[kBlock], ar[kBlock];
    int bad = 0;
    for (int f = 0; f < 48000; f += kBlock) {
        e->render(ml, mr, al, ar, kBlock);
        for (int i = 0; i < kBlock; ++i) {
            const float* x = &ref[4 * static_cast<size_t>(f + i)];
            if (ml[i] != x[0] || mr[i] != x[1] || al[i] != x[2] || ar[i] != x[3]) ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

TEST(engine_render_larger_than_prepared_block) {
    auto a = makeEngine();
    if (!a) return;
    auto b = makeEngine();
    std::vector<std::vector<float>> ao(4, std::vector<float>(2000)), bo(4, std::vector<float>(2000));
    int bad = 0;
    for (int n = 0; n < 50; ++n) {  // 2 s, past the start of the boot's sound
        a->render(ao[0].data(), ao[1].data(), ao[2].data(), ao[3].data(), 2000);
        for (int k = 0; k < 4; ++k)
            b->render(bo[0].data() + 500 * k, bo[1].data() + 500 * k, bo[2].data() + 500 * k,
                      bo[3].data() + 500 * k, 500);
        if (ao != bo) ++bad;
    }
    CHECK_EQ(bad, 0);
}

TEST(engine_boot_snapshot) {
    auto e = makeEngine();
    if (!e) return;
    renderMs(*e, 5000);
    CHECK(!e->snapshot().booted);
    renderMs(*e, kBootedMs - 5000);
    const app::Snapshot s = e->snapshot();
    CHECK(s.booted);
    std::printf("  display: [%s]\n", shown(s.panel).c_str());
    CHECK(startsWith(rowText(s.panel, 0), "U0-0 BABY-GRAND"));
    CHECK_EQ(s.panel.leds, 0x201u);  // LEDs 0 and 9
}

// DECISION A101: a byte at offset 479 enters about 479 output frames later than one at offset 0.
TEST(engine_midi_offset_places_note) {
    auto a = bootedEngine();
    if (!a) return;
    auto b = bootedEngine();
    const uint8_t on[] = {0x90, 0x3C, 0x64};
    a->midi(0, on, 3);
    b->midi(479, on, 3);
    std::vector<float> oa, ob;
    renderMs(*a, 500, &oa);
    renderMs(*b, 500, &ob);
    const int fa = firstNonZero(oa), fb = firstNonZero(ob);
    std::printf("  first sound: %d and %d\n", fa, fb);
    CHECK(fa >= 0 && fb >= 0);
    CHECK(fb - fa >= 400 && fb - fa <= 560);
}

TEST(engine_buttons_reach_system_page) {
    auto e = bootedEngine();
    if (!e) return;
    press(*e, ts::Button::System);
    const std::string s = shown(e->snapshot().panel);
    std::printf("  display: [%s]\n", s.c_str());
    CHECK(startsWith(s, "SYSTEM  TUNE="));
}

// DECISION A101: a sysex longer than DUART A's wire queue (1024) is held and fed as the wire drains: none is
// dropped, and a note-on after it plays once the wire has carried it (3000 bytes at 3125 bytes/s, about 1 s).
TEST(engine_long_sysex_is_not_dropped) {
    auto e = bootedEngine();
    if (!e) return;
    std::vector<uint8_t> sx(3000, 0x00);
    sx[0] = 0xF0;
    sx[1] = 0x7D;
    sx.back() = 0xF7;
    e->midi(0, sx.data(), static_cast<int>(sx.size()));
    const uint8_t on[] = {0x90, 0x3C, 0x64};
    e->midi(0, on, 3);
    std::vector<float> out;
    renderMs(*e, 800, &out);
    CHECK(firstNonZero(out) < 0);  // still on the wire
    renderMs(*e, 1000, &out);
    const uint32_t dropped = e->snapshot().midiDropped;
    std::printf("  dropped: %u, first sound at %d\n", static_cast<unsigned>(dropped), firstNonZero(out));
    CHECK_EQ(dropped, 0u);
    CHECK(firstNonZero(out) >= 0);
}

// DECISION A101: bytes beyond the engine's held store (kMidiHeld) are dropped and counted.
TEST(engine_midi_overflow_is_counted) {
    auto e = bootedEngine();
    if (!e) return;
    std::vector<uint8_t> flood(Engine::kMidiHeld + 100, 0x00);
    flood[0] = 0xF0;
    e->midi(0, flood.data(), static_cast<int>(flood.size()));
    renderMs(*e, 10);
    CHECK_EQ(e->snapshot().midiDropped, 100u);
}

// A full event queue at once: the panel link takes only whole events, and the rest wait.
TEST(engine_posts_never_split_a_panel_event) {
    auto e = bootedEngine();
    if (!e) return;
    for (int i = 0; i < 256; ++i) CHECK(e->post({Event::Kind::Key, 60, i % 2 == 0, 100}));
    CHECK(!e->post({Event::Kind::Key, 60, false, 100}));
    renderMs(*e, 1000);
    CHECK_EQ(e->machine().panelDropped(), 0u);
    CHECK_EQ(e->machine().duart().rxDropped(ts::kPanelChannel), 0u);
    CHECK(e->snapshot().midiOutBytes > 0);  // the keys played: the firmware sends them on MIDI out
}

TEST(engine_ram_handoff) {
    auto e = makeEngine();
    if (!e) return;
    renderMs(*e, 100);
    std::vector<uint8_t> v;
    CHECK(!e->takeRam(v));
    e->requestRam();
    renderMs(*e, 10);
    CHECK(e->takeRam(v));
    CHECK(v == e->machine().ramImage());
    CHECK(!e->takeRam(v));
}

// ---- The current sound, the requested sound, analogs, the note mask (A105, A106, A107).

namespace {
void midiBytes(Engine& e, std::initializer_list<uint8_t> v) {
    const std::vector<uint8_t> b(v);
    e.midi(0, b.data(), static_cast<int>(b.size()));
}
// setProgram before every block, for ms.
void renderWanting(Engine& e, int n, int ms) {
    for (int i = 0; i < ms / 10; ++i) {
        e.setProgram(n);
        renderMs(e, 10);
    }
}
std::vector<uint8_t> selectOf(const app::Snapshot& s) { return {s.select.begin(), s.select.begin() + s.selectLen}; }
}  // namespace

TEST(engine_tracks_boot_as_program_zero) {
    auto e = makeEngine();
    if (!e) return;
    renderMs(*e, 5000);
    CHECK_EQ(e->snapshot().program, -1);
    renderMs(*e, kBootedMs - 5000);
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.program, 0);
    CHECK_EQ(s.programsSent, 0u);
    CHECK_EQ(s.selectLen, 0);
}

TEST(engine_tracks_panel_pick) {
    auto e = bootedEngine();
    if (!e) return;
    press(*e, ts::Button::Soft2);
    renderMs(*e, 200);
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.program, 1);
    CHECK(selectOf(s) == (std::vector<uint8_t>{0xB0, 0x00, 0x00, 0x20, 0x00, 0xC0, 0x01}));
}

TEST(engine_tracks_midi_in_program) {
    auto e = bootedEngine();
    if (!e) return;
    midiBytes(*e, {0xC0, 0x05});
    renderMs(*e, 500);
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.program, 5);
    CHECK(startsWith(rowText(s.panel, 0), "U0-0"));
    CHECK_EQ(s.programsSent, 0u);
}

TEST(engine_restores_program_after_warm_boot) {
    auto a = bootedEngine();
    if (!a) return;
    midiBytes(*a, {0xC0, 0x20});
    renderMs(*a, 500);
    CHECK(startsWith(rowText(a->snapshot().panel, 0), "U0-5"));
    const std::vector<uint8_t> ram = a->machine().ramImage();
    auto b = makeEngine();
    std::string err;
    CHECK(b->loadRam(ram, err));
    renderWanting(*b, 32, kBootedMs + 500);
    const app::Snapshot s = b->snapshot();
    CHECK(startsWith(rowText(s.panel, 0), "U0-5"));
    CHECK_EQ(s.program, 32);
    CHECK_EQ(s.programsSent, 1u);
}

TEST(engine_panel_pick_is_not_undone) {
    auto e = bootedEngine();
    if (!e) return;
    renderWanting(*e, 0, 100);
    press(*e, ts::Button::Soft2);
    CHECK_EQ(e->snapshot().program, 1);
    renderWanting(*e, 0, 500);
    CHECK_EQ(e->snapshot().program, 1);
    CHECK_EQ(e->snapshot().programsSent, 0u);
    renderWanting(*e, 1, 100);
    CHECK_EQ(e->snapshot().programsSent, 0u);
    renderWanting(*e, 7, 500);  // bank 1, sound 1
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.programsSent, 1u);
    CHECK(startsWith(rowText(s.panel, 0), "U0-1"));
    CHECK_EQ(s.program, 7);
}

// programSeq counts tracked changes (panel, MIDI in), not the engine's own sends nor the OS answering C0 05
// with B0 00 00 20 00 C0 05 on MIDI out; adoptProgram never sends.
TEST(engine_program_seq_and_adopt) {
    auto e = bootedEngine();
    if (!e) return;
    const uint32_t s0 = e->snapshot().programSeq;
    press(*e, ts::Button::Soft2);
    renderMs(*e, 100);
    midiBytes(*e, {0xC0, 0x05});
    renderMs(*e, 200);
    CHECK_EQ(e->snapshot().programSeq, s0 + 2);
    for (int i = 0; i < 10; ++i) {
        e->adoptProgram(9);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->snapshot().programsSent, 0u);
    CHECK_EQ(e->snapshot().program, 5);
    renderWanting(*e, 7, 200);  // a real send
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.programsSent, 1u);
    CHECK_EQ(s.program, 7);
    CHECK_EQ(s.programSeq, s0 + 2);
}

// Plays the panel steps on a fresh engine, takes the tracked select and the RAM, and replays them with
// setSelect on another engine after its warm boot: the display comes back as it was, with one send.
static void roundTrip(const std::vector<ts::Button>& steps, const std::vector<uint8_t>& expectSelect, int program,
                      int row, const char* rowStart) {
    auto a = bootedEngine();
    if (!a) return;
    for (const auto b : steps) press(*a, b);
    renderMs(*a, 300);
    const app::Snapshot sa = a->snapshot();
    std::printf("  tracked:");
    for (const uint8_t b : selectOf(sa)) std::printf(" %02X", b);
    std::printf("  display: [%s]\n", shown(sa.panel).c_str());
    CHECK(selectOf(sa) == expectSelect);
    CHECK(startsWith(rowText(sa.panel, row), rowStart));
    CHECK_EQ(sa.program, program);
    const std::vector<uint8_t> ram = a->machine().ramImage();
    auto b = makeEngine();
    std::string err;
    CHECK(b->loadRam(ram, err));
    b->setSelect(sa.select.data(), sa.selectLen);
    renderWanting(*b, program, kBootedMs + 500);
    const app::Snapshot sb = b->snapshot();
    std::printf("  restored: [%s]\n", shown(sb.panel).c_str());
    CHECK(shown(sb.panel) == shown(sa.panel));
    CHECK_EQ(sb.program, program);
    CHECK_EQ(sb.programsSent, 1u);
    CHECK(selectOf(sb) == expectSelect);
}

TEST(engine_restores_bank_and_preset) {
    // R2-2: the ROM bank set (bank LSB 2), bank 2, sound 3.
    roundTrip({ts::Button::BankSet, ts::Button::BankSet, ts::Button::Bank2, ts::Button::Soft3},
              {0xB0, 0x00, 0x00, 0x20, 0x02, 0xC0, 0x0E}, 14, 0, "R2-2");
    // A preset: 60 + 6 x bank 3 + slot 3.
    roundTrip({ts::Button::Presets, ts::Button::Bank3, ts::Button::Soft4},
              {0xB0, 0x00, 0x00, 0x20, 0x00, 0xC0, 0x51}, 81, 1, "PSET");
}

TEST(engine_analog_waits_for_boot) {
    auto e = makeEngine();
    if (!e) return;
    for (int i = 0; i < 100; ++i) {
        e->setAnalog(ts::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ts::Analog::PitchWheel), 0x200);
    for (int i = 100; i < kBootedMs / 10; ++i) {
        e->setAnalog(ts::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ts::Analog::PitchWheel), 0x100);
}

TEST(engine_note_mask) {
    auto e = bootedEngine();
    if (!e) return;
    auto notes = [&] {
        renderMs(*e, 10);
        return e->snapshot().notes;
    };
    midiBytes(*e, {0x90, 0x3C, 0x64});
    CHECK_EQ(notes()[0], uint64_t{1} << 60);
    midiBytes(*e, {0x94, 0x40, 0x64});
    CHECK_EQ(notes()[1], uint64_t{1});
    midiBytes(*e, {0x80, 0x3C, 0x00});
    CHECK_EQ(notes()[0], uint64_t{0});
    midiBytes(*e, {0x94, 0x40, 0x00});
    CHECK_EQ(notes()[1], uint64_t{0});
    midiBytes(*e, {0x90, 0x3C, 0x64});
    renderMs(*e, 10);
    midiBytes(*e, {0xB0, 0x7B, 0x00});
    const auto n = notes();
    CHECK(n[0] == 0 && n[1] == 0);
}

TEST(engine_load_ram_resets_analogs) {
    auto e = bootedEngine();
    if (!e) return;
    for (int i = 0; i < 10; ++i) {
        e->setAnalog(ts::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ts::Analog::PitchWheel), 0x100);
    const std::vector<uint8_t> ram = e->machine().ramImage();
    std::string err;
    CHECK(e->loadRam(ram, err));
    for (int i = 0; i < 100; ++i) {
        e->setAnalog(ts::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ts::Analog::PitchWheel), 0x200);
    for (int i = 100; i < kBootedMs / 10; ++i) {
        e->setAnalog(ts::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ts::Analog::PitchWheel), 0x100);
}

TEST(engine_set_select_ignores_bad_bytes) {
    auto a = bootedEngine();
    if (!a) return;
    const std::vector<uint8_t> ram = a->machine().ramImage();
    auto b = makeEngine();
    std::string err;
    CHECK(b->loadRam(ram, err));
    const uint8_t bad[] = {0x90, 0x3C, 0x64, 0xC0, 0x0C};
    b->setSelect(bad, 5);
    renderWanting(*b, 12, kBootedMs + 500);  // a bare C0 0C instead: bank 2, sound 0
    const app::Snapshot s = b->snapshot();
    CHECK_EQ(s.programsSent, 1u);
    CHECK(startsWith(rowText(s.panel, 0), "U0-2"));
    CHECK(s.notes[0] == 0 && s.notes[1] == 0);
    // The OS answered the bare C0 0C on MIDI out with its full selection, which the tracker took.
    CHECK(selectOf(s) == (std::vector<uint8_t>{0xB0, 0x00, 0x00, 0x20, 0x00, 0xC0, 0x0C}));
}

// The Seqs/Songs LED is DUART OP3 (interface doc, "Display and LEDs"), not one of the panel's LEDs.
TEST(engine_seq_led) {
    auto e = bootedEngine();
    if (!e) return;
    CHECK(!e->snapshot().seqLed);
    press(*e, ts::Button::SeqsSongs);
    CHECK(e->snapshot().seqLed);
    press(*e, ts::Button::Sounds);
    CHECK(!e->snapshot().seqLed);
}

// The left patch select button makes the OS send B0 46 40 (ts_patch_select_is_read).
TEST(engine_patch_select_reaches_machine) {
    auto e = bootedEngine();
    if (!e) return;
    const uint64_t b0 = e->snapshot().midiOutBytes;
    renderMs(*e, 100);
    const uint64_t b1 = e->snapshot().midiOutBytes;
    CHECK_EQ(b1, b0);
    CHECK(e->post({Event::Kind::PatchSelect, 0, true, 0}));
    renderMs(*e, 100);
    const uint64_t b2 = e->snapshot().midiOutBytes;
    std::printf("  MIDI out bytes: %llu\n", static_cast<unsigned long long>(b2 - b1));
    CHECK(b2 >= b1 + 3);
    e->post({Event::Kind::PatchSelect, 0, false, 0});
    renderMs(*e, 100);
}

// DECISION A133: MIDI out reaches the host as whole messages, in order.
std::vector<std::vector<uint8_t>> g_out;
void collectOut(void*, const uint8_t* d, int n) { g_out.emplace_back(d, d + n); }

TEST(engine_midi_out_whole_messages) {
    auto e = bootedEngine();
    if (!e) return;
    e->setMidiOut(collectOut, nullptr);
    g_out.clear();
    CHECK(e->post({Event::Kind::Key, 60, true, 100}));
    renderMs(*e, 100);
    CHECK(e->post({Event::Kind::Key, 60, false, 100}));
    renderMs(*e, 100);
    bool on = false;
    for (const auto& m : g_out) on |= m.size() == 3 && (m[0] & 0xF0) == 0x90 && m[1] == 60 && m[2] > 0;
    std::printf("  messages: %zu\n", g_out.size());
    CHECK(on);
    // Identity Request (MIDI 1.0 universal sysex): the OS answers with Identity Reply, F0 7E .. 06 02 .. F7,
    // once SYS-EX=ON (MIDI Control page 3; OFF in a new RAM).
    for (int i = 0; i < 3; ++i) press(*e, ts::Button::MidiControl);
    press(*e, ts::Button::Soft1);
    press(*e, ts::Button::Up);
    CHECK(shown(e->snapshot().panel).find("SYS-EX=ON") != std::string::npos);
    g_out.clear();
    const uint8_t ask[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    const uint64_t before = e->snapshot().midiOutBytes;
    e->midi(0, ask, 6);
    renderMs(*e, 2000);
    std::printf("  out bytes after the request: %llu, dropped in: %u\n",
                static_cast<unsigned long long>(e->snapshot().midiOutBytes - before), e->snapshot().midiDropped);
    bool reply = false;
    for (const auto& m : g_out) {
        std::printf("  out:");
        for (const uint8_t b : m) std::printf(" %02X", b);
        std::printf("\n");
        reply |= m.size() >= 6 && m[0] == 0xF0 && m[1] == 0x7E && m[3] == 0x06 && m[4] == 0x02 && m.back() == 0xF7;
    }
    CHECK(reply);
    e->setMidiOut(nullptr, nullptr);
}

// DECISION A105: the restore goes out once, at the first block start once booted, and a note shortly
// after it is heard (the OS ignores notes for about 50 ms after a program change that loads an effect).
TEST(engine_restore_happens_once_at_boot_end) {
    auto a = bootedEngine();
    if (!a) return;
    midiBytes(*a, {0xC0, 0x20});
    renderMs(*a, 500);
    const std::vector<uint8_t> ram = a->machine().ramImage();
    auto b = makeEngine();
    std::string err;
    CHECK(b->loadRam(ram, err));
    renderWanting(*b, 32, 5250);
    CHECK_EQ(b->snapshot().programsSent, 0u);
    std::vector<float> before, after;
    int wrong = 0;
    for (int ms = 5250; ms < 6000; ms += 10) {
        if (ms == 5500) midiBytes(*b, {0x90, 0x3C, 0x64});
        b->setProgram(32);
        renderMs(*b, 10, ms < 5500 ? &before : &after);
        if (ms + 10 >= 5400 && b->snapshot().programsSent != 1u) ++wrong;
    }
    CHECK_EQ(wrong, 0);
    float peakBefore = 0, peakAfter = 0;
    for (const float x : before) peakBefore = std::max(peakBefore, std::fabs(x));
    for (const float x : after) peakAfter = std::max(peakAfter, std::fabs(x));
    std::printf("  peak before the note %.5f, after %.5f\n", peakBefore, peakAfter);
    CHECK(peakAfter > 0.01f && peakAfter > 10 * peakBefore);
}

// DECISION A118: disks cross threads through single-slot atomics; the block start applies them.
TEST(engine_insert_applies_at_block) {
    auto e = makeEngine();
    if (!e) return;
    auto a = ts::Disk::formatted(ts::Density::Double);
    auto b = ts::Disk::formatted(ts::Density::High);
    const ts::Disk* pa = a.get();
    const ts::Disk* pb = b.get();
    CHECK(!e->snapshot().diskIn);
    CHECK(e->insertDisk(std::move(a)));
    CHECK(e->machine().disk() == nullptr);  // not before the block
    renderMs(*e, 10);
    CHECK(e->snapshot().diskIn);
    CHECK(e->machine().disk() == pa);
    CHECK_EQ(e->snapshot().diskSwaps, 1u);
    CHECK(e->takeRemoved() == nullptr);
    CHECK(e->insertDisk(std::move(b)));
    renderMs(*e, 10);
    CHECK(e->machine().disk() == pb);
    auto old = e->takeRemoved();
    CHECK(old.get() == pa);
    CHECK(e->takeRemoved() == nullptr);
    CHECK_EQ(e->snapshot().diskSwaps, 2u);
    CHECK(e->insertDisk(nullptr));  // eject
    renderMs(*e, 10);
    CHECK(!e->snapshot().diskIn);
    CHECK(e->takeRemoved().get() == pb);
}

TEST(engine_insert_twice_before_render) {
    auto e = makeEngine();
    if (!e) return;
    CHECK(e->insertDisk(ts::Disk::formatted(ts::Density::Double)));
    CHECK(!e->insertDisk(ts::Disk::formatted(ts::Density::Double)));
    renderMs(*e, 10);
    CHECK(e->insertDisk(nullptr));
}

TEST(engine_copy_out) {
    auto e = makeEngine();
    if (!e) return;
    auto d = ts::Disk::formatted(ts::Density::Double);
    d->block(7)[3] = 0x5A;
    e->insertDisk(std::move(d));
    renderMs(*e, 10);
    CHECK(e->requestDisk(ts::Disk::blank(ts::Density::Double)));
    CHECK(!e->requestDisk(ts::Disk::blank(ts::Density::Double)));  // one pending
    std::unique_ptr<ts::Disk> out;
    bool ok = false; uint32_t sw = 0;
    CHECK(!e->takeDisk(out, ok, sw));  // not before the block
    renderMs(*e, 10);
    CHECK(e->takeDisk(out, ok, sw));
    CHECK(ok && out);
    CHECK_EQ(out->block(7)[3], 0x5A);
    CHECK(!e->takeDisk(out, ok, sw));
}

// A119: the write-protect request reaches the disk in the drive at the next block, after a swap in the same
// block, so a disk inserted and protected before one render comes out protected.
TEST(engine_write_protect_at_block) {
    auto e = makeEngine();
    if (!e) return;
    e->insertDisk(ts::Disk::formatted(ts::Density::Double));
    e->setWriteProtect(true);
    CHECK(e->machine().disk() == nullptr);
    renderMs(*e, 10);
    CHECK(e->machine().disk() && e->machine().disk()->writeProtected());
    e->setWriteProtect(false);
    renderMs(*e, 10);
    CHECK(!e->machine().disk()->writeProtected());
}

// A119: a restart takes the disk out of the old engine, a pending insert or eject applied first.
TEST(engine_release_disk) {
    auto e = makeEngine();
    if (!e) return;
    CHECK(e->releaseDisk() == nullptr);
    auto d = ts::Disk::formatted(ts::Density::Double);
    const ts::Disk* raw = d.get();
    e->insertDisk(std::move(d));
    CHECK(e->releaseDisk().get() == raw);  // pending: the disk asked for
    e->insertDisk(ts::Disk::formatted(ts::Density::High));
    renderMs(*e, 10);
    auto out = e->releaseDisk();
    CHECK(out && out->density() == ts::Density::High && e->machine().disk() == nullptr);
    e->insertDisk(std::move(out));
    renderMs(*e, 10);
    e->insertDisk(nullptr);
    CHECK(e->releaseDisk() == nullptr);  // a pending eject
}

TEST(engine_copy_out_density_mismatch) {
    auto e = makeEngine();
    if (!e) return;
    e->insertDisk(ts::Disk::formatted(ts::Density::Double));
    renderMs(*e, 10);
    CHECK(e->requestDisk(ts::Disk::blank(ts::Density::High)));
    renderMs(*e, 10);
    std::unique_ptr<ts::Disk> out;
    bool ok = true; uint32_t sw = 0;
    CHECK(e->takeDisk(out, ok, sw));
    CHECK(!ok);
    CHECK(out != nullptr);  // the scratch comes back to be freed here
    e->insertDisk(nullptr);
    renderMs(*e, 10);
    CHECK(e->requestDisk(ts::Disk::blank(ts::Density::Double)));
    renderMs(*e, 10);
    CHECK(e->takeDisk(out, ok, sw));
    CHECK(!ok);  // an empty drive
}

TEST(engine_insert_into_empty_drive_does_not_stall) {
    auto e = makeEngine();
    if (!e) return;
    CHECK(e->insertDisk(ts::Disk::formatted(ts::Density::Double)));
    renderMs(*e, 10);
    CHECK(e->insertDisk(ts::Disk::formatted(ts::Density::High)));  // no takeRemoved in between
    renderMs(*e, 10);
    CHECK_EQ(e->snapshot().diskSwaps, 2u);
    CHECK(e->takeRemoved() != nullptr);  // the first disk, owed to the caller
}

TEST(engine_copy_identifies_its_disk) {
    auto e = makeEngine();
    if (!e) return;
    auto a = ts::Disk::formatted(ts::Density::Double);
    a->block(5)[0] = 0x11;
    e->insertDisk(std::move(a));
    renderMs(*e, 10);  // swap 1
    auto b = ts::Disk::formatted(ts::Density::Double);
    b->block(5)[0] = 0x22;
    CHECK(e->requestDisk(ts::Disk::blank(ts::Density::Double)));
    CHECK(e->insertDisk(std::move(b)));  // before the block that serves the copy
    renderMs(*e, 10);
    std::unique_ptr<ts::Disk> out;
    bool ok = false;
    uint32_t sw = 0;
    CHECK(e->takeDisk(out, ok, sw));
    CHECK(ok && out);
    CHECK_EQ(sw, 2u);
    CHECK_EQ(out->block(5)[0], 0x22);
}

TEST(engine_destroy_with_pending_slots) {
    auto e = makeEngine();
    if (!e) return;
    e->insertDisk(ts::Disk::formatted(ts::Density::Double));
    renderMs(*e, 10);
    e->insertDisk(ts::Disk::formatted(ts::Density::Double));
    renderMs(*e, 10);  // the first disk is now an untaken removed disk
    e->requestDisk(ts::Disk::blank(ts::Density::Double));
    renderMs(*e, 10);  // an untaken copy
    e->insertDisk(ts::Disk::formatted(ts::Density::Double));  // and a pending insert
    e.reset();         // frees all four (checked by sanitizers and leak tools)
    CHECK(true);
}

// DECISION A128: the CD drive's slot works as the disk's (A118): applied at the next block start, one at a time.
TEST(engine_cd_insert_at_block) {
    auto e = makeEngine(true);
    if (!e) return;
    CHECK(e->cdPort() != nullptr);
    CHECK_EQ(e->snapshot().cdMedium, 0u);
    CHECK(e->insertCd(9, 100));
    CHECK(!e->insertCd(10, 50));  // one pending
    CHECK_EQ(e->snapshot().cdMedium, 0u);
    renderMs(*e, 10);
    CHECK_EQ(e->snapshot().cdMedium, 9u);
    CHECK(e->machine().cd()->hasDisc());
    CHECK(e->insertCd(0, 0));  // eject
    renderMs(*e, 10);
    CHECK_EQ(e->snapshot().cdMedium, 0u);
    CHECK(!e->machine().cd()->hasDisc());
}

TEST(engine_release_cd) {
    auto e = makeEngine(true);
    if (!e) return;
    CHECK(e->insertCd(9, 100));
    auto pending = e->releaseCd();  // a pending insert is taken back unapplied
    CHECK_EQ(pending.first, 9u);
    CHECK_EQ(pending.second, 100u);
    renderMs(*e, 10);
    CHECK_EQ(e->snapshot().cdMedium, 0u);
    CHECK(e->insertCd(4, 20));
    renderMs(*e, 10);
    auto cur = e->releaseCd();  // otherwise the disc in the drive
    CHECK_EQ(cur.first, 4u);
    CHECK_EQ(cur.second, 20u);
    auto plain = makeEngine();  // no SCSI board: no port, nothing to release
    if (!plain) return;
    CHECK(plain->cdPort() == nullptr);
    CHECK(plain->releaseCd() == (std::pair<uint32_t, uint32_t>{0, 0}));
}

TEST(engine_disk_reads_in_snapshot) {
    auto e = makeEngine();
    if (!e) return;
    auto d = ts::Disk::formatted(ts::Density::Double);
    d->noteRead(3);
    d->noteRead(7);
    CHECK(e->insertDisk(std::move(d)));
    renderMs(*e, 10);
    CHECK_EQ(e->snapshot().diskReads, 2u);
}

// T31: a TS-12 engine boots the TS-12 OS and plays its low E (note 28, below the TS-10's range).
TEST(engine_ts12_plays_low_e) {
    ts::RomSet roms;
    if (!readTsRoms(roms, ts::Model::Ts12)) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    std::string err;
    auto e = Engine::create(roms, err, false, false, ts::Model::Ts12);
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(e->model() == ts::Model::Ts12);
    e->prepare(48000, kBlock);
    renderMs(*e, kBootedMs);
    e->setMidiOut(collectOut, nullptr);
    g_out.clear();
    CHECK(e->post({Event::Kind::Key, 28, true, 99}));
    renderMs(*e, 200);
    bool on = false;
    for (const auto& m : g_out) on |= m.size() == 3 && m[0] == 0x90 && m[1] == 0x1C && m[2] > 0;
    CHECK(on);
    e->setMidiOut(nullptr, nullptr);
}
