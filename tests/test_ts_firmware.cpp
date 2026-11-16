// The TS-10 OS v3.10 booting on the machine. The firmware is run, never studied: these tests look
// only at device accesses, exception counts, device state, the panel and MIDI out.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "files.h"
#include "smf.h"
#include "ts_firmware.h"
#include "ts_render.h"

using namespace ens::ts;

namespace {

// The ACTV the firmware writes at boot: 0x1F (32 voices) to location 0x58 byte 3 (0x30005F) with PAGE
// below 0x20 (docs/ts-hardware-interface.md, "OTTO voice count"). ACTV also resets to 0x1F, so the
// write itself is checked, not only the end state.
constexpr uint8_t kBootActv = 0x1F;
constexpr int kBootVoices = kBootActv + 1;
constexpr uint16_t kIllegalOpcode = 0x4AFC;  // ILLEGAL

// What the shared boot did, besides the machine's own counters.
struct BootSeen {
    uint32_t txBytes[2] = {0, 0};   // characters sent on DUART channels A (MIDI) and B (to the panel)
    uint32_t badIllegal = 0;        // vector 4 taken at an opcode other than ILLEGAL
    uint32_t actvWrites = 0;        // byte-3 writes to ACTV (location 0x58 while PAGE < 0x20)
    int lastActv = -1;              // the last value written there
};
BootSeen bootSeen;

// One machine booted for kBootMs, shared by the tests that only read its state; nullptr without ROMs.
Machine* bootedTs() {
    static const std::unique_ptr<Machine> m = [] {
        auto b = loadTs();
        if (!b) return b;
        // The OS executes two ILLEGAL instructions on purpose at about 1 s, and its handler steps over
        // them. Any other illegal-instruction exception (a bad decode, a jump into data) is counted.
        Machine& mc = *b;
        uint32_t last = 0, seen = 0;
        mc.cpu().onInstruction = [&mc, &last, &seen](uint32_t pc) {
            const uint32_t n = mc.cpu().exceptionCount(4);
            if (n != seen) {
                if (mc.peek16(last) != kIllegalOpcode) ++bootSeen.badIllegal;
                seen = n;
            }
            last = pc;
        };
        // OTTO host address HA5..HA0 = A6..A1 (T13). PAGE is location 0x78 (HA 0x3C-0x3F), ACTV location
        // 0x58 (HA 0x2C-0x2F) in pages 0x00-0x1F; a byte-3 write commits the register (notes §1, OT1).
        uint8_t page = 0;
        mc.onBusEvent = [&page](const BusEvent& e) {
            if (e.dev != Device::Otto || !e.write || e.size != 1 || !(e.addr & 1)) return;
            const unsigned ha = (e.addr >> 1) & 0x3F;
            if (ha == 0x3F) page = static_cast<uint8_t>(e.value);
            if (ha == 0x2F && (page & 0x7F) < 0x20) {
                ++bootSeen.actvWrites;
                bootSeen.lastActv = e.value;
            }
        };
        mc.onPanelByte = [](uint8_t) { ++bootSeen.txBytes[kPanelChannel]; };
        const uint64_t end = mc.cycles() + static_cast<uint64_t>(kBootMs) * kCpuHz / 1000;
        Audio a;
        while (mc.cycles() < end) {
            mc.runFrame(a);
            for (uint8_t byte; mc.duart().popTransmitted(kMidiChannel, byte);) ++bootSeen.txBytes[kMidiChannel];
        }
        mc.cpu().onInstruction = nullptr;
        mc.onBusEvent = nullptr;
        mc.onPanelByte = nullptr;
        return b;
    }();
    return m.get();
}

// The trace hash of one fresh boot, computed once.
uint64_t bootHash() {
    static const uint64_t h = [] {
        auto m = loadTs();
        return m ? traceHash(*m, kBootMs) : 0;
    }();
    return h;
}

}  // namespace

// Every access lands on a device, with the device's own width (odd bytes for OTTO, ESP and DUART).
TEST(ts_boot_is_fully_mapped) {
    auto m = loadTs();
    if (!m) return;
    uint32_t unmapped = 0, evenBytes = 0;
    m->onBusEvent = [&](const BusEvent& e) {
        if (e.dev == Device::Unmapped) {
            if (unmapped < 4) std::printf("  unmapped %c%u %06x\n", e.write ? 'W' : 'R', e.size, e.addr);
            ++unmapped;
        }
        const bool odd = e.dev == Device::Otto || e.dev == Device::Esp || e.dev == Device::Duart;
        if (odd && e.size == 1 && !(e.addr & 1)) ++evenBytes;
    };
    runMs(*m, kBootMs);
    CHECK_EQ(unmapped, 0u);
    CHECK_EQ(evenBytes, 0u);
    CHECK_EQ(m->duart().unsupportedWrites(), 0u);
}

TEST(ts_boot_takes_no_fault_vectors) {
    Machine* m = bootedTs();
    if (!m) return;
    for (int v : {2, 3, 5, 6, 7, 8, 11, 14, 15, 24}) {
        if (m->cpu().exceptionCount(v)) std::printf("  vector %d taken %u times\n", v, m->cpu().exceptionCount(v));
        CHECK_EQ(m->cpu().exceptionCount(v), 0u);
    }
    // Vector 4: only the firmware's own ILLEGAL instructions (printed, see bootedTs).
    std::printf("  vector 4 taken %u times, all at ILLEGAL: %s\n", m->cpu().exceptionCount(4),
                bootSeen.badIllegal ? "no" : "yes");
    CHECK_EQ(bootSeen.badIllegal, 0u);
    CHECK(!m->cpu().halted());
}

// The firmware releases ESPHALT, sets the voice count and uses both DUART channels: both receivers
// are enabled and both transmitters have sent (the OS disables a transmitter between messages).
TEST(ts_boot_runs_the_chips) {
    Machine* m = bootedTs();
    if (!m) return;
    CHECK(!m->board().esp().halted());
    std::printf("  ACTV written %u times, last 0x%02x\n", bootSeen.actvWrites, bootSeen.lastActv);
    CHECK(bootSeen.actvWrites > 0);
    CHECK_EQ(bootSeen.lastActv, kBootActv);
    CHECK_EQ(m->board().otto().activeVoices(), kBootVoices);
    for (int ch = 0; ch < 2; ++ch) {
        CHECK(m->duart().rxEnabled(ch));
        CHECK(bootSeen.txBytes[ch] > 0);
    }
}

TEST(ts_boot_is_deterministic) {
    auto b = loadTs();
    if (!b) return;
    std::printf("  boot trace hash %016llx\n", static_cast<unsigned long long>(bootHash()));
    CHECK_EQ(traceHash(*b, kBootMs), bootHash());
}

// Review focus 2: two machines run interleaved frame by frame trace exactly as one alone.
TEST(ts_two_machines_are_independent) {
    auto a = loadTs();
    if (!a) return;
    auto b = loadTs();
    TraceHasher ha, hb;
    a->onBusEvent = [&ha](const BusEvent& e) { ha(e); };
    b->onBusEvent = [&hb](const BusEvent& e) { hb(e); };
    const uint64_t end = a->cycles() + static_cast<uint64_t>(kBootMs) * kCpuHz / 1000;  // as runMs
    Audio out;
    while (a->cycles() < end || b->cycles() < end) {
        if (a->cycles() < end) a->runFrame(out);
        if (b->cycles() < end) b->runFrame(out);
    }
    CHECK_EQ(ha.h, bootHash());
    CHECK_EQ(hb.h, bootHash());
}

// The play screen at kBootMs, as observed from OS v3.10 (docs/ts-hardware-interface.md, "Display and
// LEDs"): Sounds mode, bank U0-0, its six sounds with BABY-GRAND selected; LEDs 0 (Bank 0) and
// 9 (Sounds). Every byte of the boot is decoded.
TEST(ts_boot_shows_play_screen) {
    Machine* m = bootedTs();
    if (!m) return;
    const PanelState& p = m->panel();
    std::printf("  |%s|\n  |%s|\n", rowText(p, 0).c_str(), rowText(p, 1).c_str());
    CHECK(rowText(p, 0) == "U0-0 BABY-GRAND   FULL-BODY   BRITE-PNO ");  // as observed
    CHECK(rowText(p, 1) == "     STEREO-PNO  WARM-GRAND  ALL-PURPOSE");  // as observed
    CHECK_EQ(p.leds, (1u << 0) | (1u << 9));
    CHECK_EQ(p.unknown, 0u);
}

namespace {

// A machine booted for kBootMs on its own, for tests that press buttons or keys; nullptr without ROMs.
std::unique_ptr<Machine> bootFresh(bool sqx70 = false) {
    auto m = loadTs(sqx70);
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

// Runs ms and returns the largest |sample| of Main L/R.
int mainPeak(Machine& m, uint32_t ms) {
    const uint64_t end = m.cycles() + static_cast<uint64_t>(ms) * kCpuHz / 1000;
    int peak = 0;
    Audio a;
    while (m.cycles() < end) {
        m.runFrame(a);
        peak = std::max({peak, std::abs(int{a.mainL}), std::abs(int{a.mainR})});
    }
    return peak;
}

}  // namespace

// Mode and page buttons show their pages (T16).
TEST(ts_button_changes_page) {
    auto m = bootFresh();
    if (!m) return;
    tap(*m, Button::System);
    std::printf("  |%s|\n", rowText(m->panel(), 0).c_str());
    CHECK(rowText(m->panel(), 0).compare(0, 12, "SYSTEM  TUNE") == 0);
    tap(*m, Button::Bank5);
    std::printf("  |%s|\n", rowText(m->panel(), 0).c_str());
    CHECK(rowText(m->panel(), 0).compare(0, 4, "U0-5") == 0);
    CHECK_EQ(m->panel().leds, (1u << 9) | (1u << 11));  // Sounds, Bank 5
    tap(*m, Button::Presets);
    CHECK(rowText(m->panel(), 1).compare(0, 4, "PSET") == 0);
    CHECK_EQ(m->panel().unknown, 0u);
}

// A key sounds and goes out on MIDI as note 60 (T17), with the volume slider at rest (up, T8).
TEST(ts_key_sounds) {
    auto m = bootFresh();
    if (!m) return;
    midiOut(*m);
    const int quiet = mainPeak(*m, 200);
    m->key(60, true, 127);
    const int loud = mainPeak(*m, 200);
    const std::vector<uint8_t> on = midiOut(*m);
    m->key(60, false, 0);
    runMs(*m, 50);
    const std::vector<uint8_t> off = midiOut(*m);
    std::printf("  peak before %d, during %d\n", quiet, loud);
    CHECK(quiet < 64);
    CHECK(loud > 1000);
    CHECK(contains(on, {0x90, 0x3C, 0x7F}));
    CHECK(contains(off, {0x80, 0x3C, 0x40}));
}

// Poly-Key pressure of a held key goes out as MIDI polyphonic aftertouch (T17).
TEST(ts_poly_pressure_is_sent) {
    auto m = bootFresh();
    if (!m) return;
    midiOut(*m);
    m->key(64, true, 64);
    runMs(*m, 20);
    m->pressure(64, 100);
    runMs(*m, 20);
    m->pressure(64, 0);
    runMs(*m, 20);
    const std::vector<uint8_t> out = midiOut(*m);
    CHECK(contains(out, {0xA0, 0x40, 100, 0x40, 0}));
}

namespace {

// Holds a while b is tapped (60 ms), then releases both: the firmware's two-button shortcuts.
void chord(Machine& m, Button a, Button b) {
    m.button(a, true);
    runMs(m, 60);
    tap(m, b);
    m.button(a, false);
    runMs(m, 300);
}

// Text that must never appear in normal operation: a low-battery warning, a SYSTEM ERROR, an
// unexpected-event message, a missing keyboard or disk drive that answers wrongly.
const char* badText(const PanelState& p) {
    for (const char* t : {"BATTERY", "ERROR", "EVENT", "NOT RESPONDING"})
        if (shows(p, t)) return t;
    return nullptr;
}

// Runs ms; the first bad text seen on any frame (nullptr if none).
const char* watchMs(Machine& m, uint32_t ms) {
    const uint64_t end = m.cycles() + static_cast<uint64_t>(ms) * kCpuHz / 1000;
    const char* bad = nullptr;
    Audio a;
    while (m.cycles() < end) {
        m.runFrame(a);
        if (!bad) bad = badText(m.panel());
    }
    return bad;
}

// The peak of Main L/R over 200 ms of a C4 struck at full strike, then released.
int notePeak(Machine& m) {
    m.key(60, true, 127);
    const int peak = mainPeak(m, 200);
    m.key(60, false, 0);
    runMs(m, 600);
    return peak;
}

}  // namespace

// The analog resting values (T8) give a power-on with no warning or error text on any frame.
TEST(ts_cold_boot_is_clean) {
    auto m = loadTs();
    if (!m) return;
    const char* bad = watchMs(*m, kBootMs);
    if (bad) std::printf("  saw \"%s\"\n", bad);
    CHECK(bad == nullptr);
    CHECK(rowText(m->panel(), 0).compare(0, 4, "U0-0") == 0);
}

// The Analog Test page (service manual p28: hold Presets, press Compare) with the inputs at rest
// (T8): wheels centred and down, CV pedal unplugged (PED=127), volume up, a good battery (BAT below
// 010) and REF in 180-220. KNOB shows 000 until the slider moves.
TEST(ts_analog_resting_values) {
    auto m = bootFresh();
    if (!m) return;
    chord(*m, Button::Presets, Button::Compare);
    runMs(*m, 3000);  // the readings are filtered
    const std::string r0 = rowText(m->panel(), 0), r1 = rowText(m->panel(), 1);
    std::printf("  |%s|\n  |%s|\n", r0.c_str(), r1.c_str());
    CHECK(r0 == "ANALOG  PTCH=064 MOD=000 PED=127 VOL=127");
    CHECK(r1.compare(0, 21, "TESTS   KNOB=000 REF=") == 0);
    const int ref = std::atoi(r1.substr(21, 3).c_str());
    CHECK(ref >= 180 && ref <= 220);
    CHECK(r1.compare(24, 9, " BAT=000 ") == 0);
}

// A pitch wheel back at its rest value after a move, as a released wheel springs back, sends the centre
// bend (MSB 0x40). The OS sets the wheel's dead zone at power-on from 8-sample averages of REF and the
// wheel, then scales every reading by REF's filtered value; a stale first REF sample (the battery's
// conversion) left the dead zone 1/8 off once REF settled, and the wheel sent MSB 0x32-0x36 back at rest (OT16).
TEST(ts_pitch_wheel_returns_to_centre) {
    auto m = bootFresh();
    if (!m) return;
    runMs(*m, 3000);  // REF's filter settled
    const uint16_t rest = kAnalogRest[static_cast<size_t>(Analog::PitchWheel)];
    for (const int away : {0x000, 0x340}) {
        m->setAnalog(Analog::PitchWheel, static_cast<uint16_t>(away));
        runMs(*m, 500);
        midiOut(*m);
        m->setAnalog(Analog::PitchWheel, rest);
        runMs(*m, 800);
        const auto out = midiOut(*m);  // E0 lsb msb, then running status: the last byte is the last MSB
        std::printf("  back from %03x: %zu bytes, last %02x\n", away, out.size(), out.empty() ? 0 : out.back());
        CHECK(!out.empty() && out.back() == 0x40);
    }
}

// The Volume slider scales a struck note: silent at 0, loud at 0x3FF; a request beyond 10 bits is
// clamped to the maximum, not wrapped.
TEST(ts_slider_moves_volume) {
    auto m = bootFresh();
    if (!m) return;
    int peak[3];
    const uint16_t raw[3] = {0x3FF, 0, 0x400};
    for (int i = 0; i < 3; ++i) {
        m->setAnalog(Analog::Volume, raw[i]);
        runMs(*m, 300);
        peak[i] = notePeak(*m);
    }
    std::printf("  peak at 0x3FF %d, 0 %d, 0x400 %d\n", peak[0], peak[1], peak[2]);
    CHECK(peak[0] > 1000);
    CHECK(peak[1] < 64);
    CHECK(peak[2] > 1000);
}

// The patch select buttons are read on IEXP (T9): the OS sends them as MIDI controller 70 (Musician's
// Manual, "Using the Patch Select Buttons"): left 0x40, right 0x20, both 0x7F, none 0.
TEST(ts_patch_select_is_read) {
    auto m = bootFresh();
    if (!m) return;
    midiOut(*m);
    const auto step = [&m](int side, bool down) {
        m->patchSelect(side, down);
        runMs(*m, 50);
        return midiOut(*m);
    };
    const std::vector<uint8_t> left = step(0, true), none = step(0, false), right = step(1, true),
                               both = step(0, true);
    CHECK(contains(left, {0xB0, 0x46, 0x40}));
    CHECK(contains(none, {0x46, 0x00}));
    CHECK(contains(right, {0x46, 0x20}));
    CHECK(contains(both, {0x46, 0x7F}));
}

// Foot Switch 2 is read on IEXP (T9): with SW2-R=SUSTAIN on the System page, footswitch 3 (FSW3B/)
// sends sustain and footswitch 4 (FSW4B/) does not, so 3 is the right contact.
TEST(ts_footswitch2_is_read) {
    auto m = bootFresh();
    if (!m) return;
    tap(*m, Button::System);
    tap(*m, Button::System);  // FOOT-SW1-L, R, PEDAL, SW2-L, SW2-R
    tap(*m, Button::Soft6);
    tap(*m, Button::Up);
    CHECK(rowText(m->panel(), 1).find("R=SUSTAIN") != std::string::npos);
    midiOut(*m);
    m->footswitch(3, true);
    runMs(*m, 50);
    CHECK(contains(midiOut(*m), {0xB0, 0x40, 0x7F}));
    m->footswitch(3, false);
    runMs(*m, 50);
    CHECK(contains(midiOut(*m), {0x40, 0x00}));
    m->footswitch(4, true);
    runMs(*m, 50);
    CHECK(midiOut(*m).empty());
    m->footswitch(4, false);
}

// With the drive empty (T10) a disk command from the panel (Storage, Storage: the disk page and its
// directory) gets the firmware's no-disk answer, DISK DRIVE NOT READY (Musician's Manual, "Storage":
// "No disk in the drive"), then the disk page, with no error code.
TEST(ts_floppy_reports_no_disk) {
    auto m = bootFresh();
    if (!m) return;
    tap(*m, Button::Storage);
    m->button(Button::Storage, true);
    runMs(*m, 60);
    m->button(Button::Storage, false);
    bool notReady = false;
    const char* bad = nullptr;
    const uint64_t end = m->cycles() + static_cast<uint64_t>(5000) * kCpuHz / 1000;
    Audio a;
    while (m->cycles() < end) {
        m->runFrame(a);
        notReady = notReady || shows(m->panel(), "DISK DRIVE NOT READY");
        if (!bad) bad = badText(m->panel());
    }
    std::printf("  |%s|\n", rowText(m->panel(), 0).c_str());
    CHECK(notReady);
    CHECK(bad == nullptr);
    CHECK(rowText(m->panel(), 0).compare(0, 11, "DISK   FORM") == 0);
}

// ---- MIDI (Task 7) ------------------------------------------------------------------------------

namespace {

void sendBytes(Machine& m, std::initializer_list<uint8_t> bytes) {
    for (const uint8_t b : bytes) m.midiIn(b);
}

// One booted machine for the MIDI checks, run in sequence at construction (one boot). Channel 1:
//   200 ms of silence; middle C at velocity 100 for 300 ms, its note-off, then 1 s;
//   a burst of 33 note-ons and 33 note-offs (198 bytes) queued at once, so they enter back to back at
//   the line rate, then 2 s watching the display; 2 s more and the peak over 200 ms (released);
//   program change 5, then 500 ms.
struct MidiRig {
    std::unique_ptr<Machine> m;
    std::vector<uint8_t> bootOut;  // what the boot sent on MIDI out
    int silence = 0, noteOn = 0, released = 0;
    uint32_t overruns = 0, dropped = 0;
    const char* bad = nullptr;  // the first warning or error text seen after the burst
    PanelState beforeProgram{}, afterProgram{};
};

MidiRig& midiTs() {
    static MidiRig r = [] {
        MidiRig x;
        x.m = loadTs();
        if (!x.m) return x;
        Machine& m = *x.m;
        runMs(m, kBootMs);
        x.bootOut = midiOut(m);
        x.silence = mainPeak(m, 200);
        sendBytes(m, {0x90, 0x3C, 0x64});
        x.noteOn = mainPeak(m, 300);
        sendBytes(m, {0x80, 0x3C, 0x40});
        runMs(m, 1000);
        for (uint8_t k = 0; k < 33; ++k) sendBytes(m, {0x90, static_cast<uint8_t>(36 + k), 100});
        for (uint8_t k = 0; k < 33; ++k) sendBytes(m, {0x80, static_cast<uint8_t>(36 + k), 64});
        const uint64_t end = m.cycles() + 2ull * kCpuHz;
        Audio a;
        while (m.cycles() < end) {
            m.runFrame(a);
            if (!x.bad) x.bad = badText(m.panel());
            if (!x.bad && shows(m.panel(), "BUFFER")) x.bad = "BUFFER";
        }
        x.overruns = m.duart().overruns(kMidiChannel);
        x.dropped = m.duart().rxDropped(kMidiChannel);
        runMs(m, 2000);
        x.released = mainPeak(m, 200);
        x.beforeProgram = m.panel();
        sendBytes(m, {0xC0, 0x05});
        runMs(m, 500);
        x.afterProgram = m.panel();
        return x;
    }();
    if (!r.m) std::printf("  skip: ROMs not found\n");
    return r;
}

// Both rows, then each row's underline and flashing columns as hex masks.
std::string shown(const PanelState& p) {
    char a[80];
    std::snprintf(a, sizeof a, " u %010llx %010llx b %010llx %010llx", static_cast<unsigned long long>(p.underline[0]),
                  static_cast<unsigned long long>(p.underline[1]), static_cast<unsigned long long>(p.blink[0]),
                  static_cast<unsigned long long>(p.blink[1]));
    return "|" + rowText(p, 0) + "|" + rowText(p, 1) + "|" + a;
}

// The main PCM of tests/data/player_smoke.mid (copied from KS-32 Reborn) rendered after a fresh boot,
// with a 2 s tail, as `tsemu render` does by default; empty without ROMs.
std::vector<int16_t> renderSmoke() {
    std::vector<uint8_t> file;
    std::vector<MidiEvent> events;
    std::string err;
    CHECK(readFile(std::string(ENS_TEST_DATA_DIR) + "/player_smoke.mid", file, err));
    CHECK(parseSmf(file, events, err));
    auto m = loadTs();
    if (!m) return {};
    runMs(*m, kBootMs);
    return renderMidi(*m, events, 2000).main;
}

}  // namespace

// T20: channel A runs from the 500 kHz external 16x clock: 128 X1 clocks per bit, 31 250 baud. The
// boot sends something on MIDI out, starting with a status byte.
TEST(ts_midi_rate) {
    MidiRig& r = midiTs();
    if (!r.m) return;
    CHECK_EQ(r.m->duart().rxBitClocks(kMidiChannel), 128u);
    std::printf("  boot MIDI out: %zu bytes, first %02X\n", r.bootOut.size(), r.bootOut.empty() ? 0 : r.bootOut[0]);
    CHECK(!r.bootOut.empty() && (r.bootOut[0] & 0x80));
}

TEST(ts_midi_note_sounds) {
    MidiRig& r = midiTs();
    if (!r.m) return;
    std::printf("  peak: silence %d, note %d\n", r.silence, r.noteOn);
    CHECK(r.silence < 64);
    CHECK(r.noteOn > 1000);
}

// Review focus 4: a burst queued at once enters at the line rate (D2) and none of it is lost: no
// overrun, nothing dropped from the wire queue, no warning or "out of buffers" text, and the notes
// release.
TEST(ts_midi_burst_no_overrun) {
    MidiRig& r = midiTs();
    if (!r.m) return;
    std::printf("  overruns %u, dropped %u, peak 4 s after the burst %d\n", r.overruns, r.dropped, r.released);
    if (r.bad) std::printf("  saw \"%s\"\n", r.bad);
    CHECK_EQ(r.overruns, 0u);
    CHECK_EQ(r.dropped, 0u);
    CHECK(r.bad == nullptr);
    CHECK(r.released < 256);
}

// Program change 5 selects the bank's sixth sound, ALL-PURPOSE, which the play screen then underlines
// (as observed; T20).
TEST(ts_program_change_changes_display) {
    MidiRig& r = midiTs();
    if (!r.m) return;
    std::printf("  %s -> %s\n", shown(r.beforeProgram).c_str(), shown(r.afterProgram).c_str());
    CHECK(shown(r.afterProgram) != shown(r.beforeProgram));
    const size_t col = rowText(r.afterProgram, 1).find("ALL-PURPOSE");
    CHECK(col != std::string::npos);
    if (col != std::string::npos) CHECK_EQ(r.afterProgram.underline[1], 0x7FFull << col);
    CHECK_EQ(r.afterProgram.underline[0], 0ull);
}

namespace {

// One 32-bit OTTO register of a copy of the chip (the machine's OTTO is untouched): PAGE = page, then
// bytes 0-3 of register r (OT1).
uint32_t ottoReg(ens::Otto& c, uint8_t page, int r) {
    c.write(0x3C, 0);
    c.write(0x3D, 0);
    c.write(0x3E, 0);
    c.write(0x3F, page);
    const auto b = static_cast<uint8_t>(r * 4);
    return static_cast<uint32_t>(c.read(b)) << 24 | static_cast<uint32_t>(c.read(b + 1)) << 16 |
           static_cast<uint32_t>(c.read(b + 2)) << 8 | c.read(b + 3);
}

}  // namespace

// OT9: programs 8 (HONKY-TONK, LP4:LP3 = 00) and 50 (WOW-SWEEP, 01) give sounding voices high-pass
// poles with K2 up to 0xFF00. Over one note each, no running voice with volume has pole 4's output
// (O4(n-1), page 0x20 register 4) on the 18-bit rail; with the literal "+0.5" it sat at +-131071.
TEST(ts_hp_voices_stay_off_the_rail) {
    auto m = bootFresh();
    if (!m) return;
    for (const uint8_t prog : {uint8_t{8}, uint8_t{50}}) {
        sendMidi(*m, 0xC0, prog, 0);
        runMs(*m, 300);
        sendMidi(*m, 0x90, 60, 100);
        int32_t peak = 0;
        uint32_t hpLooks = 0;
        Audio a;
        for (int f = 0; f < 600 * 30; ++f) {  // about 600 ms
            m->runFrame(a);
            if (f % 8) continue;
            ens::Otto c(m->board().otto());
            for (uint8_t v = 0; v < 32; ++v) {
                const uint32_t cr = ottoReg(c, v, 0);
                if ((cr & 3) || !((ottoReg(c, v, 2) | ottoReg(c, v, 4)) & 0xFFFF)) continue;  // stopped or silent
                if (cr & 0x200) continue;  // LP4: pole 4 is low-pass
                ++hpLooks;
                const uint32_t o4 = ottoReg(c, static_cast<uint8_t>(0x20 | v), 4) & 0x3FFFF;
                const int32_t s = (o4 & 0x20000) ? static_cast<int32_t>(o4) - 0x40000 : static_cast<int32_t>(o4);
                peak = std::max(peak, std::abs(s));
            }
        }
        sendMidi(*m, 0x80, 60, 64);
        runMs(*m, 500);
        std::printf("  program %d: %u looks at HP voices, |O4| max %d\n", prog, hpLooks, peak);
        CHECK(hpLooks > 0);
        CHECK(peak < 131071);
    }
}

TEST(ts_render_is_deterministic) {
    const std::vector<int16_t> a = renderSmoke();
    if (a.empty()) return;
    const std::vector<int16_t> b = renderSmoke();
    std::printf("  %zu frames, render hash %016llx\n", a.size() / 2, static_cast<unsigned long long>(pcmHash(a)));
    CHECK(a == b);
}

// ---- Battery RAM (Task 8) -----------------------------------------------------------------------

namespace {

// Counts into bad each vector-4 exception taken at an opcode other than ILLEGAL (see bootedTs).
void watchIllegal(Machine& mc, uint32_t& bad) {
    uint32_t last = 0, seen = mc.cpu().exceptionCount(4);  // counts survive a power cycle
    mc.cpu().onInstruction = [&mc, &bad, last, seen](uint32_t pc) mutable {
        const uint32_t n = mc.cpu().exceptionCount(4);
        if (n != seen) {
            if (mc.peek16(last) != kIllegalOpcode) ++bad;
            seen = n;
        }
        last = pc;
    };
}

}  // namespace

// T13: the battery RAM keeps the settings. Master Tune is set to +02 on the System page (Up twice),
// the RAM is saved, and a fresh machine powered on from the image (a power cycle keeping the RAM) boots
// with no warning or error text and shows TUNE=+02 on its System page.
TEST(ts_ram_image_keeps_settings) {
    auto a = bootFresh();
    if (!a) return;
    tap(*a, Button::System);
    const std::string cold = rowText(a->panel(), 0);
    tap(*a, Button::Up);
    tap(*a, Button::Up);
    const std::string set = rowText(a->panel(), 0);
    const std::vector<uint8_t> img = a->ramImage();

    auto m = loadTs();
    std::string err;
    CHECK(m->loadRamImage(img, err));
    const char* bad = watchMs(*m, kBootMs);
    const std::string booted = rowText(m->panel(), 0);
    tap(*m, Button::System);
    const std::string tune = rowText(m->panel(), 0);
    std::printf("  cold |%s|\n  set  |%s|\n  boot |%s|\n  warm |%s|\n", cold.c_str(), set.c_str(), booted.c_str(),
                tune.c_str());
    if (bad) std::printf("  saw \"%s\"\n", bad);
    CHECK(cold.compare(0, 16, "SYSTEM  TUNE=+00") == 0);
    CHECK(set.compare(0, 16, "SYSTEM  TUNE=+02") == 0);
    CHECK(bad == nullptr);
    CHECK(tune.compare(0, 16, "SYSTEM  TUNE=+02") == 0);
}

// Review focus 5: a power-on with garbage in the battery RAM (pseudo-random bytes, fixed seed) boots
// to the play screen and takes no fault vector.
TEST(ts_garbage_ram_boots) {
    auto m = loadTs();
    if (!m) return;
    std::vector<uint8_t> img(m->ramImage().size());
    uint32_t x = 0x7E5710u;
    for (uint8_t& b : img) {
        x = x * 1664525u + 1013904223u;  // Numerical Recipes LCG; the top byte is the best mixed
        b = static_cast<uint8_t>(x >> 24);
    }
    std::string err;
    CHECK(m->loadRamImage(img, err));
    uint32_t badIllegal = 0;
    watchIllegal(*m, badIllegal);
    const char* bad = watchMs(*m, kBootMs);
    m->cpu().onInstruction = nullptr;
    std::printf("  |%s|\n  |%s|\n", rowText(m->panel(), 0).c_str(), rowText(m->panel(), 1).c_str());
    if (bad) std::printf("  saw \"%s\"\n", bad);
    for (int v : {2, 3, 5, 6, 7, 8, 11, 14, 15, 24}) {
        if (m->cpu().exceptionCount(v)) std::printf("  vector %d taken %u times\n", v, m->cpu().exceptionCount(v));
        CHECK_EQ(m->cpu().exceptionCount(v), 0u);
    }
    CHECK_EQ(badIllegal, 0u);
    CHECK(!m->cpu().halted());
    CHECK(bad == nullptr);
    CHECK(rowText(m->panel(), 0) == "U0-0 BABY-GRAND   FULL-BODY   BRITE-PNO ");
}

namespace {

// What recording twenty notes on SEQUENCE-01 (Record with Play) and keeping them (YES) did: the FREE
// count on the third Sequencer Control page before and after, the prompt at Stop, and the battery image
// before and after.
struct Kept {
    int freeBefore = 0, freeAfter = 0;
    std::string ask;
    std::vector<uint8_t> before, after;
};

int freeEvents(Machine& m) {
    for (int i = 0; i < 3; ++i) tap(m, Button::SequencerControl);
    const std::string r = rowText(m.panel(), 0);
    const size_t at = r.find("FREE=");
    return at == std::string::npos ? -1 : std::atoi(r.c_str() + at + 5);
}

Kept recordAndKeep(Machine& m) {
    Kept k;
    k.freeBefore = freeEvents(m);
    k.before = m.ramImage();
    tap(m, Button::SeqsSongs);
    tap(m, Button::Soft2);  // SEQUENCE-01
    chord(m, Button::Record, Button::Play);
    runMs(m, 3000);
    for (int n = 0; n < 20; ++n) {
        m.key(48 + n, true, 80);
        runMs(m, 20);
        m.key(48 + n, false, 0);
        runMs(m, 20);
    }
    tap(m, Button::StopContinue);
    k.ask = rowText(m.panel(), 0);
    tap(m, Button::Soft3);  // *YES*
    runMs(m, 2000);
    k.freeAfter = freeEvents(m);
    k.after = m.ramImage();
    return k;
}

int nonzero(const std::vector<uint8_t>& img, size_t from, size_t to) {
    return static_cast<int>(std::count_if(img.begin() + from, img.begin() + to, [](uint8_t b) { return b != 0; }));
}

}  // namespace

// T13, stock TS (the default, design section 4): SEQEXP's sockets are empty, the OS's probe at 0x038000
// fails, it offers 31 775 free events (the manual's 30,000 notes) and stores a kept sequence from the top
// of its OSRAM sequencer memory (0x037FFF) down. The image's SEQEXP half stays zero.
TEST(ts_stock_sequence_is_kept_in_osram) {
    auto m = bootFresh();
    if (!m) return;
    const Kept k = recordAndKeep(*m);
    std::printf("  FREE %d -> %d; |%s|; nonzero at 0x37000-0x37FFF %d -> %d, in SEQEXP %d\n", k.freeBefore,
                k.freeAfter, k.ask.c_str(), nonzero(k.before, 0x37000, 0x38000), nonzero(k.after, 0x37000, 0x38000),
                nonzero(k.after, 0x40000, 0x80000));
    CHECK_EQ(k.freeBefore, 31775);
    CHECK(k.ask.compare(0, 18, "KEEP FIRST TRACK ?") == 0);
    CHECK(k.freeAfter > 0 && k.freeAfter < k.freeBefore);
    CHECK(nonzero(k.after, 0x37000, 0x38000) > nonzero(k.before, 0x37000, 0x38000));
    CHECK_EQ(nonzero(k.after, 0x40000, 0x80000), 0);
}

// T13, SQX-70 fitted: the OS finds SEQEXP by its probe at 0x038000, offers 97 311 free events (the
// manual's "over 97,000 notes") and stores a kept sequence from the top of SEQEXP (0x077FFF) down, so it
// lands in the image's SEQEXP half (0x77000-0x77FFF at 0x7F000).
TEST(ts_sqx70_sequence_is_kept_in_seqexp) {
    auto m = bootFresh(true);
    if (!m) return;
    const Kept k = recordAndKeep(*m);
    std::printf("  FREE %d -> %d; |%s|; nonzero at 0x77000-0x77FFF %d -> %d\n", k.freeBefore, k.freeAfter,
                k.ask.c_str(), nonzero(k.before, 0x7F000, 0x80000), nonzero(k.after, 0x7F000, 0x80000));
    CHECK_EQ(k.freeBefore, 97311);
    CHECK(k.ask.compare(0, 18, "KEEP FIRST TRACK ?") == 0);
    CHECK(k.freeAfter > 0 && k.freeAfter < k.freeBefore);
    CHECK(nonzero(k.after, 0x7F000, 0x80000) > nonzero(k.before, 0x7F000, 0x80000));
}

// T13: loadRamImage on a running machine is the whole power cycle. A note leaves a tail in the ESP's
// delay DRAM, Master Tune is set to +02, and the machine is powered on again from its own image: it
// boots with no fault vector or bad text, keeps TUNE=+02, and its audio from power-on matches a fresh
// machine loaded with the same image (nothing volatile survives).
TEST(ts_power_cycle_on_the_same_machine) {
    auto a = bootFresh();
    if (!a) return;
    tap(*a, Button::System);
    tap(*a, Button::Up);
    tap(*a, Button::Up);
    a->key(60, true, 127);
    runMs(*a, 300);
    a->key(60, false, 0);
    runMs(*a, 100);
    const std::vector<uint8_t> img = a->ramImage();
    uint32_t faults[256];
    for (int v = 0; v < 256; ++v) faults[v] = a->cpu().exceptionCount(v);
    std::string err;
    CHECK(a->loadRamImage(img, err));
    uint32_t badIllegal = 0;
    watchIllegal(*a, badIllegal);
    const std::vector<int16_t> pcmA = renderMidi(*a, {}, kBootMs).main;
    a->cpu().onInstruction = nullptr;
    for (int v : {2, 3, 5, 6, 7, 8, 11, 14, 15, 24}) CHECK_EQ(a->cpu().exceptionCount(v), faults[v]);
    CHECK_EQ(badIllegal, 0u);
    CHECK(badText(a->panel()) == nullptr);
    tap(*a, Button::System);
    const std::string tune = rowText(a->panel(), 0);

    auto b = loadTs();
    CHECK(b->loadRamImage(img, err));
    const std::vector<int16_t> pcmB = renderMidi(*b, {}, kBootMs).main;
    std::printf("  |%s|; boot pcm %016llx vs fresh %016llx\n", tune.c_str(),
                static_cast<unsigned long long>(pcmHash(pcmA)), static_cast<unsigned long long>(pcmHash(pcmB)));
    CHECK(tune.compare(0, 16, "SYSTEM  TUNE=+02") == 0);
    CHECK(pcmA == pcmB);
}

// E32: OS v3.10 reads every ESP register it loads back and compares; on a mismatch it reloads, up to 10
// times (counter at RAM $130), then gives up with the flag at $12e set and never writes the effect's
// tables. Effect 64 GUITAR AMP 2 (U1-3 CRUNCH-ELEC, bank LSB 1 program 19) writes ABASE = 0x3CFE00 and
// reads its waveshaper from table A: with a failed load it is silent. Effect 71 EQ--COMPRESSOR (U1-8
// ZEP-KIT, program 48) reads its gain curve from table A the same way.
TEST(ts_effect_tables_load) {
    auto m = bootFresh();
    if (!m) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    const auto failFlag = [&] { return m->peek16(0x12E) >> 8; };
    sendBytes(*m, {0xB0, 0x00, 0x00, 0xB0, 0x20, 0x01, 0xC0, 19});
    runMs(*m, 600);
    const unsigned crunchFail = failFlag();
    for (int k : {40, 47, 52, 55}) sendBytes(*m, {0x90, static_cast<uint8_t>(k), 100});
    const int crunch = mainPeak(*m, 500);
    for (int k : {40, 47, 52, 55}) sendBytes(*m, {0x80, static_cast<uint8_t>(k), 64});
    sendBytes(*m, {0xC0, 48});
    runMs(*m, 600);
    const unsigned zepFail = failFlag();
    std::printf("  CRUNCH-ELEC: load failed %u, peak %d; ZEP-KIT: load failed %u\n", crunchFail, crunch, zepFail);
    CHECK_EQ(crunchFail, 0u);
    CHECK(crunch > 1000);
    CHECK_EQ(zepFail, 0u);
}

// E33: the same load verification compares CMR's top byte against `written | 0x03`. U0-4 SUITCASE-74
// (program 24) and U0-5 TRANS-CLAV (program 34) load a CMR value; with bits 17..16 reading 0 the OS
// retried 10 times and dropped the rest of the effect's parameters. RAM $130 counts the retries of the
// last load (cleared when a load starts).
TEST(ts_effect_loads_verify_first_time) {
    auto m = bootFresh();
    if (!m) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    for (int prog : {24, 34}) {
        sendBytes(*m, {0xB0, 0x00, 0x00, 0xB0, 0x20, 0x00, 0xC0, static_cast<uint8_t>(prog)});
        runMs(*m, 700);
        const unsigned tries = m->peek16(0x130) >> 8;
        std::printf("  U0 program %d: load retries %u\n", prog, tries);
        CHECK_EQ(tries, 0u);
    }
}

