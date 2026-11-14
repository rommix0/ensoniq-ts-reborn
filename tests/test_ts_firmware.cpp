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
#include "ts_firmware.h"

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
std::unique_ptr<Machine> bootFresh() {
    auto m = loadTs();
    if (m) runMs(*m, kBootMs);
    return m;
}

void tap(Machine& m, Button b) {
    m.button(b, true);
    runMs(m, 60);
    m.button(b, false);
    runMs(m, 300);
}

std::vector<uint8_t> midiOut(Machine& m) {
    std::vector<uint8_t> out;
    for (uint8_t b; m.duart().popTransmitted(kMidiChannel, b);) out.push_back(b);
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

bool shows(const PanelState& p, const char* text) {
    return rowText(p, 0).find(text) != std::string::npos || rowText(p, 1).find(text) != std::string::npos;
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
    runMs(*m, 3000);  // the readings are filtered; REF settles in about 2.2 s
    const std::string r0 = rowText(m->panel(), 0), r1 = rowText(m->panel(), 1);
    std::printf("  |%s|\n  |%s|\n", r0.c_str(), r1.c_str());
    CHECK(r0 == "ANALOG  PTCH=064 MOD=000 PED=127 VOL=127");
    CHECK(r1.compare(0, 21, "TESTS   KNOB=000 REF=") == 0);
    const int ref = std::atoi(r1.substr(21, 3).c_str());
    CHECK(ref >= 180 && ref <= 220);
    CHECK(r1.compare(24, 9, " BAT=000 ") == 0);
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
