#pragma once
// Standard MIDI File reader for tsemu render (copied from KS-32 Reborn src/player): channel messages
// with frame times. The TS frame (32 voices of 16 CLKs at 15 238 090 Hz, T1) is the KS-32 chip frame.
#include <cstdint>
#include <string>
#include <vector>

struct MidiEvent {
    uint64_t frame;  // chip frame (30476180 / 1024 Hz) since the start of the file
    uint8_t status, d1, d2;
};

// Formats 0 and 1 with a PPQ division. Events of all tracks are merged by frame, stable by track
// then file order. Meta and sysex events are skipped (only FF 51 tempo is used). Never throws and
// never reads past the buffer; on failure returns false with `err` set and `out` empty.
bool parseSmf(const std::vector<uint8_t>& file, std::vector<MidiEvent>& out, std::string& err);
