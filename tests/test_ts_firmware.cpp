// The TS-10 OS v3.10 booting on the machine. The firmware is run, never studied: these tests look
// only at device accesses, exception counts and device state after kBootMs.
#include <cstdint>
#include <cstdio>
#include <memory>

#include "check.h"
#include "ts_firmware.h"

using namespace ens::ts;

namespace {

// The ACTV the firmware writes at boot (page 0, location 0x58 = 0x1F; docs/ts-hardware-interface.md, "OTTO").
constexpr int kBootVoices = 32;
constexpr uint16_t kIllegalOpcode = 0x4AFC;  // ILLEGAL

// What the shared boot did, besides the machine's own counters.
struct BootSeen {
    uint32_t txBytes[2] = {0, 0};   // characters sent on DUART channels A and B
    uint32_t badIllegal = 0;        // vector 4 taken at an opcode other than ILLEGAL
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
        const uint64_t end = mc.cycles() + static_cast<uint64_t>(kBootMs) * kCpuHz / 1000;
        Audio a;
        while (mc.cycles() < end) {
            mc.runFrame(a);
            for (int ch = 0; ch < 2; ++ch)
                for (uint8_t byte; mc.duart().popTransmitted(ch, byte);) ++bootSeen.txBytes[ch];
        }
        mc.cpu().onInstruction = nullptr;
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
