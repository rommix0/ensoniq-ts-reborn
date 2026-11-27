// tsemu: runs the emulated TS-10 (OS v3.10 on the 68000 with the DUART and the OTTO/ESP board).
//   tsemu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] [--count <n>] [--last <n>]
//              [--display-only] [--press <ms>:<name>[+<name>...]]... [--analog <name>=<raw>]... [--ram <file>] [--sqx70] [--simm8] [--ts12]
//              [--disk <image>] [--save-disk <file>] [--cd <image>]
// --trace takes a comma list of otto,esp,duart,fdc,iexp,scsi,unmapped,iack; matching bus events are
// printed as "<cycle> <DEV> <R|W><size> <addr> <value>". --last <n> prints the last n instruction
// addresses disassembled, to see where a run stops (the disassembler is a debugging aid).
// The summary lists exceptions per vector, unmapped address runs, DUART register writes, OTTO active
// voices, ESP halted and the bytes sent on DUART channels A and B. File I/O lives here; src/ts stays pure.
// The panel display is printed as "<ms> |<row>|" lines each time it changes (bytes outside 0x20-0x7E as
// \xNN; a line below marks underlined columns '_' and flashing ones '*'), the LEDs as "<ms> LEDs ..."
// each time they change; --display-only leaves out the summary.
// --press presses the named buttons (kButtons names, e.g. SYSTEM, BANK5, SOFT1) in order at <ms> from
// power-on, holds them 100 ms and releases them in reverse order; it may be repeated.
// --analog sets an analog input to a raw 10-bit value (0-1023) from power-on instead of its resting
// value (kAnalogRest): PITCH, MOD, VOLUME, CV, DATA, BATTERY, REF or PRESS; it may be repeated.
// --ram <file> is the battery RAM (Machine::ramImage, 512 KB raw: OSRAM then SEQEXP, T13): loaded at
// power-on if the file exists (otherwise a line says so and the RAM starts blank, a cold start) and written
// back when boot or render ends without an error and the run covered at least kBootMs of emulated time (render
// always does); a shorter boot --ms prints a line and leaves the file alone, so a diagnostic run cannot
// replace a good settings image with a half-initialised one. --sqx70 fits the SQX-70 sequence expander (SEQEXP);
// the default is a stock TS-10 (T13). Both use the same image layout. --simm8 fits the 4M x 8 SIMM pair (8 MB of
// sample RAM, BankSets S8 and S9) in place of the factory 2 MB (T4). --ts12 runs the TS-12 (76 keys, key release
// velocity, pressure) from os/ts12_310h.bin and ts12_310l.bin; --analog PRESS=<raw> sets its pressure input (T31).
// --disk <image> puts a disk image (raw .img, GKH, Giebler EDE/EDA or HFE, told apart by content, T21, T30) in the
// drive from power-on; the file is only read. --save-disk <file> writes the disk as it is at the end of the run, in the
// format the file's extension gives (.gkh GKH, .ede/.eda/.edt Giebler, .hfe HFE, anything else raw; the --disk file's
// header is kept when the format is the same); it is refused when it names the --disk file, and when the drive
// is empty.
// --cd <image> fits the SP-4 SCSI board (T25) and puts a CD image (.iso, or .cue with one MODE1/2352 track) in the
// CD-ROM drive at SCSI ID 4 from power-on; its sectors are read from the file between frames (T29). Only read.
//   tsemu render <in.mid> <out.wav> [--roms <dir>] [--tail <ms>] [--aux <aux-out.wav>] [--analog <name>=<raw>]...
//                [--ram <file>] [--sqx70] [--simm8] [--ts12] [--disk <image>] [--save-disk <file>] [--cd <image>]
// render boots for kBootMs (not written), sends each MIDI file event to MIDI in at its frame and renders
// until the last event plus --tail (default 2000 ms); it writes Main as a 16-bit stereo WAV at kWavRate
// (--aux writes Aux the same way) and prints one summary line and "pcm hash <16 hex>", the FNV-1a 64 of
// Main's samples. MIDI in takes channel messages only: system messages are not sent. (On Windows a file
// named aux.<anything> is the AUX device and never completes: give --aux another name.)
//   tsemu bench [--roms <dir>] [--seconds <n>] [--ts12]
// bench boots (timed and printed on its own line), then plays a fixed 8-note chord from MIDI in every
// second for n emulated seconds (default 20) and prints "emulated <s>, host <s>, realtime x<f>".
// Errors print "error: <msg>" and exit 2.
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cd_file.h"
#include "files.h"
#include "m68k/m68k_dis.h"
#include "smf.h"
#include "ts/disk_image.h"
#include "ts/machine.h"
#include "ts_render.h"
#include "wav.h"

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
    bool displayOnly = false;
    std::vector<std::pair<uint64_t, std::vector<Button>>> presses;  // --press: ms, buttons
    std::vector<std::pair<Analog, uint16_t>> analog;                  // --analog: input, raw
    std::vector<std::string> positional;                              // render: <in.mid> <out.wav>
    std::string aux;                                                  // render --aux
    uint32_t tailMs = 2000;                                           // render --tail
    uint64_t seconds = 20;                                            // bench --seconds
    std::string ram;                                                  // --ram: the battery RAM image file
    bool sqx70 = false;                                               // --sqx70: SEQEXP fitted
    bool simm8 = false;                                               // --simm8: 8 MB of sample RAM (T4)
    bool ts12 = false;                                                // --ts12: the TS-12 and its OS pair (T31)
    std::string disk, saveDisk;                                       // --disk, --save-disk
    std::string cd;                                                   // --cd: a CD image at SCSI ID 4
};

// --cd: the image answering the machine's sector port, called after every frame (T29).
struct ServedCd {
    CdFile file;
    uint8_t buf[SectorPort::kBytes] = {};
    static constexpr uint32_t kMedium = 1;
    void serve(Machine& m) {
        SectorPort* p = m.cdPort();
        uint32_t medium = 0, lba = 0;
        if (!p || !p->pending(medium, lba)) return;
        p->reply(medium, lba, buf, medium == kMedium && file.read(lba, buf));
    }
};

constexpr struct {
    Analog input;
    const char* name;
} kAnalogNames[] = {{Analog::PitchWheel, "PITCH"}, {Analog::ModWheel, "MOD"},     {Analog::Volume, "VOLUME"},
                    {Analog::CvPedal, "CV"},       {Analog::DataEntry, "DATA"},  {Analog::Battery, "BATTERY"},
                    {Analog::Reference, "REF"}, {Analog::Pressure, "PRESS"}};

uint64_t parseNum(const std::string& opt, const std::string& v) {
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0') fail("bad value '" + v + "' for " + opt);
    return n;
}

// "<name>[+<name>...]", names as in kButtons (case-insensitive).
std::vector<Button> parseButtons(const std::string& v) {
    std::vector<Button> out;
    for (size_t q = 0;;) {
        const size_t next = v.find('+', q);
        std::string name = v.substr(q, next == std::string::npos ? std::string::npos : next - q);
        for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const ButtonName* hit = nullptr;
        for (const ButtonName& b : kButtons)
            if (name == b.name) hit = &b;
        if (!hit) fail("unknown button '" + name + "'");
        out.push_back(hit->button);
        if (next == std::string::npos) return out;
        q = next + 1;
    }
}

// "<ms>:<name>[+<name>...]"
std::pair<uint64_t, std::vector<Button>> parsePress(const std::string& v) {
    const size_t colon = v.find(':');
    if (colon == std::string::npos) fail("bad value '" + v + "' for --press (<ms>:<name>[+<name>...])");
    return {parseNum("--press", v.substr(0, colon)), parseButtons(v.substr(colon + 1))};
}

// One display row, bytes outside 0x20-0x7E as \xNN.
std::string rowString(const PanelState& p, int row) {
    std::string s;
    for (char c : rowText(p, row)) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u < 0x7F) {
            s += c;
        } else {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02X", u);
            s += b;
        }
    }
    return s;
}

// A row's attributes as '*' (flashing), '_' (underlined) or ' ' per column; "" when it has none.
std::string marks(const PanelState& p, int row) {
    if (!p.underline[row] && !p.blink[row]) return "";
    std::string s(kPanelCols, ' ');
    for (int c = 0; c < kPanelCols; ++c)
        s[c] = (p.blink[row] >> c & 1) ? '*' : (p.underline[row] >> c & 1) ? '_' : ' ';
    return s;
}

// "LEDs 0 9 14*": the lit LED numbers, * = flashing; "LEDs off" when none is lit.
std::string ledString(uint32_t leds) {
    std::string s = "LEDs";
    for (int n = 0; n < kPanelLeds; ++n)
        if (leds >> n & 1) s += " " + std::to_string(n) + ((leds >> (16 + n) & 1) ? "*" : "");
    return s == "LEDs" ? "LEDs off" : s;
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

// "<name>=<raw>", raw 0-1023.
std::pair<Analog, uint16_t> parseAnalog(const std::string& v) {
    const size_t eq = v.find('=');
    std::string name = v.substr(0, eq);
    for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (eq != std::string::npos)
        for (const auto& a : kAnalogNames)
            if (name == a.name) {
                const uint64_t raw = parseNum("--analog", v.substr(eq + 1));
                if (raw > 1023) fail("bad value '" + v + "' for --analog (raw 0-1023)");
                return {a.input, static_cast<uint16_t>(raw)};
            }
    fail("bad value '" + v + "' for --analog (PITCH, MOD, VOLUME, CV, DATA, BATTERY, REF or PRESS (TS-12)=<raw>)");
}

// kept: the --disk file's format and header, for --save-disk; cd: the --cd image, opened and inserted.
std::unique_ptr<Machine> makeMachine(const Options& o, DecodedImage* kept = nullptr, ServedCd* cd = nullptr) {
    RomSet roms;
    roms.osHigh = load(o.roms + "/os", o.ts12 ? "ts12_310h.bin" : "ts10_310h.bin");
    roms.osLow = load(o.roms + "/os", o.ts12 ? "ts12_310l.bin" : "ts10_310l.bin");
    roms.u41 = load(o.roms + "/waves", "901401_u41.bin");
    roms.u43 = load(o.roms + "/waves", "901301_u43.bin");
    roms.u45 = load(o.roms + "/waves", "901402_u45.bin");
    roms.u46 = load(o.roms + "/waves", "901302_u46.bin");
    std::string err;
    auto m = Machine::create(roms, err, o.sqx70, !o.cd.empty(), o.simm8,
                           o.ts12 ? Model::Ts12 : Model::Ts10);
    if (!m) fail(err);
    for (const auto& [input, raw] : o.analog) m->setAnalog(input, raw);
    if (!o.ram.empty()) {
        if (std::filesystem::exists(o.ram)) {
            std::vector<uint8_t> img;
            if (!readFile(o.ram, img, err) || !m->loadRamImage(img, err)) fail(err + " '" + o.ram + "'");
        } else {
            std::printf("RAM file '%s' not found: cold start (it is written at the end of a full run)\n", o.ram.c_str());
        }
    }
    if (!o.disk.empty()) {
        std::vector<uint8_t> file;
        DecodedImage img;
        if (!readFile(o.disk, file, err) || !decodeImage(file, img, err)) fail(err + " '" + o.disk + "'");
        m->insertDisk(std::move(img.disk));
        if (kept) *kept = std::move(img);
    }
    if (cd && !o.cd.empty()) {
        if (!cd->file.open(o.cd, err)) fail(err + " '" + o.cd + "'");
        m->insertCd(ServedCd::kMedium, cd->file.sectors());
    }
    return m;
}

// --ram: the RAM back to the file, but only after a run of at least kBootMs of emulated time: a shorter run
// (a diagnostic stopped mid-boot) holds a half-initialised RAM that would replace a good settings image.
void saveRam(const Machine& m, const std::string& path, uint64_t emulatedMs) {
    std::string err;
    if (!path.empty() && emulatedMs < kBootMs) {
        std::printf("RAM file '%s' not written: the run (%llu ms) is shorter than the %u ms boot\n", path.c_str(),
                    static_cast<unsigned long long>(emulatedMs), static_cast<unsigned>(kBootMs));
        return;
    }
    if (!path.empty() && !writeFile(path, m.ramImage(), err)) fail(err + " '" + path + "'");
}

// --save-disk: the disk in the drive to the file, in the format its name gives.
void saveDisk(const Machine& m, const Options& o, const DecodedImage& kept) {
    if (o.saveDisk.empty()) return;
    if (!m.disk()) fail("--save-disk: the drive is empty");
    const ImageFormat f = formatForName(o.saveDisk);
    std::string err;
    const std::vector<uint8_t> header = f == kept.format ? kept.header : std::vector<uint8_t>{};
    const std::vector<uint8_t> bytes = encodeImage(*m.disk(), f, header);
    if (bytes.empty()) fail("--save-disk: a disk of more than 80 cylinders is saved as .hfe (T30)");
    if (!writeFile(o.saveDisk, bytes, err)) fail(err + " '" + o.saveDisk + "'");
}

int boot(const Options& o) {
    std::set<Device> traced;
    if (!parseTrace(o.trace, traced))
        fail("bad value '" + o.trace + "' for --trace (otto,esp,duart,fdc,iexp,scsi,unmapped,iack)");
    DecodedImage kept;
    ServedCd cd;
    auto m = makeMachine(o, &kept, &cd);

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
    m->onPanelByte = [&tx](uint8_t b) { tx[kPanelChannel].push_back(b); };
    // Button events by ms: (button, down), in insertion order for equal times.
    std::multimap<uint64_t, std::pair<Button, bool>> events;
    for (const auto& [at, buttons] : o.presses) {
        for (const Button b : buttons) events.emplace(at, std::make_pair(b, true));
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
            events.emplace(at + 100, std::make_pair(*it, false));
    }
    PanelState panelShown{};
    bool firstFrame = true;
    Audio audio;
    while (m->cycles() < target) {
        for (const uint64_t now = m->cycles() * 1000 / kCpuHz; !events.empty() && events.begin()->first <= now;
             events.erase(events.begin()))
            m->button(events.begin()->second.first, events.begin()->second.second);
        m->runFrame(audio);
        cd.serve(*m);
        ++frames;
        for (uint8_t b = 0; m->popMidiOut(b);) tx[kMidiChannel].push_back(b);
        const PanelState& p = m->panel();
        const std::string t = std::to_string(m->cycles() * 1000 / kCpuHz);
        for (int r = 0; r < kPanelRows; ++r) {
            if (!firstFrame && std::memcmp(p.text[r], panelShown.text[r], kPanelCols) == 0 &&
                p.underline[r] == panelShown.underline[r] && p.blink[r] == panelShown.blink[r])
                continue;
            std::printf("%s |%s|\n", t.c_str(), rowString(p, r).c_str());
            const std::string a = marks(p, r);
            if (!a.empty()) std::printf("%s |%s|\n", std::string(t.size(), ' ').c_str(), a.c_str());
        }
        if (firstFrame || p.leds != panelShown.leds) std::printf("%s %s\n", t.c_str(), ledString(p.leds).c_str());
        panelShown = p;
        firstFrame = false;
    }
    saveRam(*m, o.ram, o.ms);
    saveDisk(*m, o, kept);
    if (o.displayOnly) return 0;

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
    std::printf("panel bytes not decoded: %u\n", m->panel().unknown);

    if (o.last) {
        std::printf("last %zu instructions:\n", ring.size());
        Machine& mc = *m;
        const auto rd = [&mc](uint32_t a) { return mc.peek16(a); };
        for (uint32_t pc : ring) std::printf("  %06x  %s\n", pc, ens::m68k::disassemble(rd, pc).text.c_str());
    }
    return 0;
}

void writeWav(const std::string& path, const std::vector<int16_t>& pcm) {
    std::string err;
    if (!writeFile(path, makeWav16(pcm, 2, kWavRate), err)) fail(err + " '" + path + "'");
}

int render(const Options& o) {
    if (o.positional.size() != 2) fail("render needs <in.mid> <out.wav>");
    std::vector<uint8_t> file;
    std::vector<MidiEvent> midi;
    std::string err;
    if (!readFile(o.positional[0], file, err) || !parseSmf(file, midi, err)) fail(err + " '" + o.positional[0] + "'");
    DecodedImage kept;
    ServedCd cd;
    auto m = makeMachine(o, &kept, &cd);
    const auto serve = [&cd, &m] { cd.serve(*m); };
    Audio boot;  // runMs with the CD served
    for (const uint64_t end = m->cycles() + static_cast<uint64_t>(kBootMs) * kCpuHz / 1000; m->cycles() < end;) {
        m->runFrame(boot);
        serve();
    }
    const Rendered r = renderMidi(*m, midi, o.tailMs, serve);
    writeWav(o.positional[1], r.main);
    if (!o.aux.empty()) writeWav(o.aux, r.aux);
    saveRam(*m, o.ram, kBootMs);  // render booted for kBootMs first
    saveDisk(*m, o, kept);
    const size_t frames = r.main.size() / 2;
    std::printf("%zu MIDI events, %zu frames (%.3f s at %u Hz), peak %d, MIDI out %zu bytes\n", midi.size(), frames,
                static_cast<double>(frames) / kWavRate, kWavRate, r.peak, r.midiOut);
    std::printf("pcm hash %016llx\n", static_cast<unsigned long long>(pcmHash(r.main)));
    // Bytes the OS never saw: lost to a DUART A overrun, or beyond the wire queue (T20).
    std::printf("MIDI in lost: %u overrun, %u dropped\n", m->duart().overruns(0), m->duart().rxDropped(0));
    return 0;
}

// Every second the chord C2 G2 C3 E3 G3 C4 E4 G4 (velocity 100) goes on at the start of the second and
// off 750 ms later; the audio is discarded. The boot is timed on its own line.
int bench(const Options& o) {
    if (!o.positional.empty()) fail("bench takes no positional arguments");
    if (o.seconds == 0) fail("bench needs --seconds of at least 1");
    using Clock = std::chrono::steady_clock;
    const auto secs = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    };
    auto m = makeMachine(o);
    const auto t0 = Clock::now();
    runMs(*m, kBootMs);
    const auto t1 = Clock::now();
    std::printf("boot: emulated %.3f s, host %.3f s, realtime x%.2f\n", kBootMs / 1000.0, secs(t0, t1),
                kBootMs / 1000.0 / secs(t0, t1));
    constexpr uint8_t kChord[8] = {36, 43, 48, 52, 55, 60, 64, 67};
    const uint64_t second = msFrames(1000), frames = o.seconds * second;
    int peak = 0;
    Audio a;
    const auto t2 = Clock::now();
    for (uint64_t f = 0; f < frames; ++f) {
        const uint64_t at = f % second;
        if (at == 0 || at == second * 3 / 4)
            for (const uint8_t n : kChord) sendMidi(*m, at == 0 ? 0x90 : 0x80, n, at == 0 ? 100 : 64);
        m->runFrame(a);
        for (uint8_t b = 0; m->popMidiOut(b);) {
        }
        peak = std::max({peak, std::abs(int{a.mainL}), std::abs(int{a.mainR})});
    }
    const double host = secs(t2, Clock::now()), emulated = static_cast<double>(o.seconds);
    std::printf("emulated %.3f s, host %.3f s, realtime x%.2f\n", emulated, host, emulated / host);
    std::printf("peak %d (a silent run would be 0)\n", peak);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc < 2 ? "" : argv[1];
        if (cmd != "boot" && cmd != "render" && cmd != "bench")
            fail("usage:\n  tsemu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] [--count <n>] "
                 "[--last <n>] [--display-only] [--press <ms>:<name>[+<name>...]]... [--analog <name>=<raw>]...\n"
                 "             [--ram <file>] [--sqx70] [--simm8] [--ts12] [--disk <image>] [--save-disk <file>] [--cd <image>]\n"
                 "  tsemu render <in.mid> <out.wav> [--roms <dir>] [--tail <ms>] [--aux <aux-out.wav>] "
                 "[--analog <name>=<raw>]... [--ram <file>] [--sqx70] [--simm8] [--ts12]\n"
                 "               [--disk <image>] [--save-disk <file>] [--cd <image>]\n"
                 "  tsemu bench [--roms <dir>] [--seconds <n>] [--ts12]");
        Options o;
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (a.compare(0, 2, "--") != 0) {
                o.positional.push_back(a);
                continue;
            }
            if (a == "--display-only" || a == "--sqx70" || a == "--simm8" || a == "--ts12") {
                (a == "--sqx70" ? o.sqx70 : a == "--simm8" ? o.simm8 : a == "--ts12" ? o.ts12 : o.displayOnly) = true;
                continue;
            }
            if (i + 1 >= argc) fail("option " + a + " needs a value");
            const std::string v = argv[++i];
            if (a == "--roms") o.roms = v;
            else if (a == "--ms") o.ms = parseNum(a, v);
            else if (a == "--trace") o.trace = v;
            else if (a == "--from") o.from = parseNum(a, v);
            else if (a == "--count") o.count = parseNum(a, v);
            else if (a == "--last") o.last = static_cast<uint32_t>(parseNum(a, v));
            else if (a == "--press") o.presses.push_back(parsePress(v));
            else if (a == "--analog") o.analog.push_back(parseAnalog(v));
            else if (a == "--tail") o.tailMs = static_cast<uint32_t>(parseNum(a, v));
            else if (a == "--aux") o.aux = v;
            else if (a == "--seconds") o.seconds = parseNum(a, v);
            else if (a == "--ram") o.ram = v;
            else if (a == "--disk") o.disk = v;
            else if (a == "--save-disk") o.saveDisk = v;
            else if (a == "--cd") o.cd = v;
            else fail("unknown option " + a);
        }
        std::error_code ec;
        if (!o.disk.empty() && !o.saveDisk.empty() &&
            (o.disk == o.saveDisk || std::filesystem::equivalent(o.disk, o.saveDisk, ec)))
            fail("--save-disk must not name the --disk file (the image is only read)");
        if (cmd == "render") return render(o);
        if (cmd == "bench") return bench(o);
        if (!o.positional.empty()) fail("unexpected argument '" + o.positional[0] + "'");
        return boot(o);
    } catch (const Failure& f) {
        std::fprintf(stderr, "error: %s\n", f.msg.c_str());
        return 2;
    }
}
