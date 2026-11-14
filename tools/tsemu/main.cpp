// tsemu: runs the emulated TS-10 (OS v3.10 on the 68000 with the DUART and the OTTO/ESP board).
//   tsemu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] [--count <n>] [--last <n>]
// --trace takes a comma list of otto,esp,duart,fdc,iexp,scsi,unmapped,iack; matching bus events are
// printed as "<cycle> <DEV> <R|W><size> <addr> <value>". --last <n> prints the last n instruction
// addresses disassembled, to see where a run stops (the disassembler is a debugging aid).
// The summary lists exceptions per vector, unmapped address runs, DUART register writes, OTTO active
// voices, ESP halted and the bytes sent on DUART channels A and B. File I/O lives here; src/ts stays pure.
// Errors print "error: <msg>" and exit 2.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../common/files.h"
#include "m68k/m68k_dis.h"
#include "ts/machine.h"

using namespace ens::ts;

namespace {

struct Failure {
    std::string msg;
};
[[noreturn]] void fail(const std::string& msg) { throw Failure{msg}; }

struct Options {
    std::string roms = "roms", trace;
    uint64_t ms = 1000, from = 0, count = UINT64_MAX;
    uint32_t last = 0;
};

uint64_t parseNum(const std::string& opt, const std::string& v) {
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0') fail("bad value '" + v + "' for " + opt);
    return n;
}

constexpr struct {
    Device dev;
    const char* name;
} kDevices[] = {{Device::Otto, "otto"}, {Device::Esp, "esp"}, {Device::Duart, "duart"}, {Device::Fdc, "fdc"},
                {Device::Iexp, "iexp"}, {Device::Scsi, "scsi"}, {Device::Unmapped, "unmapped"},
                {Device::Iack, "iack"}};

bool parseTrace(const std::string& list, std::set<Device>& out) {
    for (size_t p = 0; p < list.size();) {
        size_t q = list.find(',', p);
        if (q == std::string::npos) q = list.size();
        const std::string t = list.substr(p, q - p);
        bool hit = false;
        for (const auto& d : kDevices)
            if (t == d.name) {
                out.insert(d.dev);
                hit = true;
            }
        if (!hit) return false;
        p = q + 1;
    }
    return true;
}

const char* devName(Device d) {
    for (const auto& e : kDevices)
        if (e.dev == d) return e.name;
    return "?";
}

std::vector<uint8_t> load(const std::string& dir, const std::string& name) {
    std::vector<uint8_t> b;
    std::string err;
    const std::string path = dir + "/" + name;
    if (!readFile(path, b, err)) fail(err + " '" + path + "'");
    return b;
}

void printBytes(const std::vector<uint8_t>& b) {
    const size_t n = b.size() < 512 ? b.size() : 512;
    std::printf("    hex:");
    for (size_t i = 0; i < n; ++i) std::printf(" %02x", b[i]);
    std::printf("\n    txt: ");
    for (size_t i = 0; i < n; ++i) std::putchar(b[i] >= 0x20 && b[i] < 0x7F ? b[i] : '.');
    std::printf("\n");
}

int boot(const Options& o) {
    std::set<Device> traced;
    if (!parseTrace(o.trace, traced))
        fail("bad value '" + o.trace + "' for --trace (otto,esp,duart,fdc,iexp,scsi,unmapped,iack)");
    RomSet roms;
    roms.osHigh = load(o.roms + "/os", "ts10_310h.bin");
    roms.osLow = load(o.roms + "/os", "ts10_310l.bin");
    roms.u41 = load(o.roms + "/waves", "901401_u41.bin");
    roms.u43 = load(o.roms + "/waves", "901301_u43.bin");
    roms.u45 = load(o.roms + "/waves", "901402_u45.bin");
    roms.u46 = load(o.roms + "/waves", "901302_u46.bin");
    std::string err;
    auto m = Machine::create(roms, err);
    if (!m) fail(err);

    std::set<uint32_t> unmapped;
    std::map<int, uint8_t> duartLast;  // register -> last value written
    uint64_t printed = 0;
    m->onBusEvent = [&](const BusEvent& e) {
        if (e.dev == Device::Unmapped) unmapped.insert(e.addr);
        // Only writes the DUART received: odd bytes and words.
        if (e.dev == Device::Duart && e.write && ((e.addr & 1) || e.size == 2))
            duartLast[(e.addr >> 1) & 0xF] = static_cast<uint8_t>(e.value);
        if (!traced.count(e.dev) || e.cycle < o.from || printed >= o.count) return;
        ++printed;
        std::printf("%llu %s %c%u %06x %0*x\n", static_cast<unsigned long long>(e.cycle), devName(e.dev),
                    e.write ? 'W' : 'R', e.size, e.addr, e.size * 2, e.value);
    };
    std::deque<uint32_t> ring;
    if (o.last)
        m->cpu().onInstruction = [&](uint32_t pc) {
            ring.push_back(pc);
            if (ring.size() > o.last) ring.pop_front();
        };

    const uint64_t target = o.ms * kCpuHz / 1000;
    uint64_t frames = 0;
    std::vector<uint8_t> tx[2];  // DUART channels A (MIDI) and B (display link)
    Audio audio;
    while (m->cycles() < target) {
        m->runFrame(audio);
        ++frames;
        for (int ch = 0; ch < 2; ++ch)
            for (uint8_t b = 0; m->duart().popTransmitted(ch, b);) tx[ch].push_back(b);
    }

    std::printf("ran %llu frames (%llu ms), %llu CPU cycles%s\n", static_cast<unsigned long long>(frames),
                static_cast<unsigned long long>(o.ms), static_cast<unsigned long long>(m->cycles()),
                m->cpu().halted() ? ", CPU HALTED" : "");
    std::printf("exceptions:");
    bool any = false;
    for (int v = 0; v < 256; ++v)
        if (const uint32_t n = m->cpu().exceptionCount(v)) {
            std::printf(" v%d=%u", v, n);
            any = true;
        }
    std::printf("%s\n", any ? "" : " none");
    // Distinct unmapped addresses; runs closer than 3 bytes merge into first-last(count); first 16 runs.
    std::printf("unmapped addresses (%zu distinct):", unmapped.size());
    int runs = 0, shown = 0;
    for (auto it = unmapped.begin(); it != unmapped.end();) {
        const uint32_t first = *it;
        uint32_t last = first;
        size_t n = 0;
        for (; it != unmapped.end() && *it - last <= 2; ++it, ++n) last = *it;
        if (++runs <= 16) {
            std::printf(" %06x-%06x(%zu)", first, last, n);
            ++shown;
        }
    }
    if (runs > shown) std::printf(" ... +%d more runs", runs - shown);
    std::printf("%s\n", unmapped.empty() ? " none" : "");
    std::printf("DUART last write per register:");
    for (const auto& [reg, v] : duartLast) std::printf(" r%d=%02x", reg, v);
    std::printf("%s\n", duartLast.empty() ? " none" : "");
    std::printf("OTTO active voices: %d\n", m->board().otto().activeVoices());
    std::printf("ESP halted: %s\n", m->board().esp().halted() ? "yes" : "no");
    std::printf("DUART A transmitted: %zu bytes\n", tx[0].size());
    if (!tx[0].empty()) printBytes(tx[0]);
    std::printf("DUART B transmitted: %zu bytes\n", tx[1].size());
    if (!tx[1].empty()) printBytes(tx[1]);
    std::printf("DUART unsupported writes: %u\n", m->duart().unsupportedWrites());

    if (o.last) {
        std::printf("last %zu instructions:\n", ring.size());
        Machine& mc = *m;
        const auto rd = [&mc](uint32_t a) { return mc.peek16(a); };
        for (uint32_t pc : ring) std::printf("  %06x  %s\n", pc, ens::m68k::disassemble(rd, pc).text.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) != "boot")
            fail("usage:\n  tsemu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] [--count <n>] "
                 "[--last <n>]");
        Options o;
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (a.compare(0, 2, "--") != 0) fail("unexpected argument '" + a + "'");
            if (i + 1 >= argc) fail("option " + a + " needs a value");
            const std::string v = argv[++i];
            if (a == "--roms") o.roms = v;
            else if (a == "--ms") o.ms = parseNum(a, v);
            else if (a == "--trace") o.trace = v;
            else if (a == "--from") o.from = parseNum(a, v);
            else if (a == "--count") o.count = parseNum(a, v);
            else if (a == "--last") o.last = static_cast<uint32_t>(parseNum(a, v));
            else fail("unknown option " + a);
        }
        return boot(o);
    } catch (const Failure& f) {
        std::fprintf(stderr, "error: %s\n", f.msg.c_str());
        return 2;
    }
}
