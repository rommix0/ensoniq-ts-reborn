#pragma once
// Running the TS machine with MIDI in and collecting its audio: the one copy shared by the tests and
// tsemu (render, bench). No file I/O here; the caller reads the MIDI file and writes the WAV.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "smf.h"
#include "ts/machine.h"

// The WAV rate: the frame rate, 15 238 090 / 512 = 29 761.9 Hz with 32 voices (T1), rounded.
constexpr uint32_t kWavRate = 29'762;

// Frames in ms at the exact frame rate, counted as the MIDI file reader counts them (30 476 180 / 1024 Hz).
inline uint64_t msFrames(uint64_t ms) { return ms * (2ull * ens::ts::kCpuHz) / 1'024'000; }

// Runs whole frames until ms of CPU time (kCpuHz) have passed; the audio is discarded.
inline void runMs(ens::ts::Machine& m, uint32_t ms) {
    const uint64_t end = m.cycles() + static_cast<uint64_t>(ms) * ens::ts::kCpuHz / 1000;
    ens::ts::Audio a;
    while (m.cycles() < end) m.runFrame(a);
}

// One channel message to MIDI in: program change (0xC0) and channel pressure (0xD0) have one data
// byte, the others two.
inline void sendMidi(ens::ts::Machine& m, uint8_t status, uint8_t d1, uint8_t d2) {
    m.midiIn(status);
    m.midiIn(d1);
    if ((status & 0xE0) != 0xC0) m.midiIn(d2);
}

// FNV-1a 64 over the samples, each as two little-endian bytes.
inline uint64_t pcmHash(const std::vector<int16_t>& pcm) {
    uint64_t h = 14695981039346656037ull;
    for (const int16_t s : pcm)
        for (int i = 0; i < 2; ++i) {
            h ^= static_cast<uint8_t>(static_cast<uint16_t>(s) >> (8 * i));
            h *= 1099511628211ull;
        }
    return h;
}

struct Rendered {
    std::vector<int16_t> main, aux;  // interleaved L/R, one pair per frame
    size_t midiOut = 0;              // bytes the firmware sent on MIDI out
    int peak = 0;                    // largest |sample| of Main
};

// From the machine's current state (normally just booted): renders until the last event plus tailMs,
// sending each event to MIDI in at the start of its frame (frame 0 = the first frame rendered).
inline Rendered renderMidi(ens::ts::Machine& m, const std::vector<MidiEvent>& events, uint32_t tailMs) {
    const uint64_t frames = (events.empty() ? 0 : events.back().frame) + msFrames(tailMs);
    Rendered r;
    r.main.reserve(static_cast<size_t>(frames) * 2);
    r.aux.reserve(static_cast<size_t>(frames) * 2);
    size_t next = 0;
    ens::ts::Audio a;
    for (uint64_t f = 0; f < frames; ++f) {
        for (; next < events.size() && events[next].frame <= f; ++next)
            sendMidi(m, events[next].status, events[next].d1, events[next].d2);
        m.runFrame(a);
        for (uint8_t b = 0; m.popMidiOut(b);) ++r.midiOut;
        r.main.insert(r.main.end(), {a.mainL, a.mainR});
        r.aux.insert(r.aux.end(), {a.auxL, a.auxR});
        r.peak = std::max({r.peak, std::abs(int{a.mainL}), std::abs(int{a.mainR})});
    }
    return r;
}
