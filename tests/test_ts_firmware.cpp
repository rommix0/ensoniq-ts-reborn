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

// A key sounds and goes out on MIDI as note 60 (T17). The volume slider is raised first: at its
// provisional 0 (T8) the TS is silent.
TEST(ts_key_sounds) {
    auto m = bootFresh();
    if (!m) return;
    m->setAnalog(Analog::Volume, 0x3FF);
    runMs(*m, 100);
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
