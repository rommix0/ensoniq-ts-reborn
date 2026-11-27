// The TS-10 front panel transcribed from datasheets/ts10-photo.webp scaled to 2576 x 920: each rect is
// measured on that image; labels are the photo's, in its line breaks (a "\n" is a line break).
#include "app/panel_layout.h"

#include <cmath>

#include "ts/machine.h"
#include "ts/panel.h"

namespace ens::app {

namespace {

using K = ControlKind;
using ts::Analog;
using ts::Button;

constexpr int B(Button b) { return static_cast<int>(b); }
constexpr int A(Analog a) { return static_cast<int>(a); }

const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr bool isBlack(int note) {
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

// Keys: the model's white keys across 2140 units (36 for the TS-10's C2-C7, 45 for the TS-12's E1-G7), black keys
// 0.6 as wide and 0.62 as long, centred on the gap between their white keys.
constexpr int kKeysX = 335, kKeysY = 555, kKeysW = 2140, kKeysH = 350;

void block(std::vector<Control>& t, int x0, int pitch, int cols, const char* const* labels, const Button* buttons) {
    // Three rows of 36 x 14 buttons at y 138, 193, 248 with a two-line label above each; labels and
    // buttons are given row by row.
    static const int rowY[3] = {138, 193, 248};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < cols; ++c) {
            const int x = x0 + c * pitch, i = r * cols + c;
            t.push_back({K::Label, -1, {x - 4, rowY[r] - 24, 44, 22}, labels[i], Ink::Text});
            t.push_back({K::Button, B(buttons[i]), {x, rowY[r], 36, 14}, labels[i]});
        }
}

std::vector<Control> build(ts::Model m) {
    std::vector<Control> t = {
        // Decor: the disk drive (the drive's control, A119).
        // DECISION A122: the decals: the TS-10 logo (the service manual's cover logo, kept to its shape), its
        // line and the Ensoniq badge, at the photo's places.
        {K::Decor, -1, {108, 112, 310, 233}, "Disk drive"},
        // DECISION A140: each model's own logo, in the same place.
        {K::Label, -1, {2010, 152, 377, 106}, m == ts::Model::Ts12 ? "TS-12" : "TS-10", Ink::Decal},
        {K::Label, -1, {2052, 261, 330, 21}, "Performance/Composition Synthesizer", Ink::Decal},
        {K::Label, -1, {2307, 330, 162, 30}, "ensoniq", Ink::Decal},
        {K::Button, kSettingsId, {2330, 80, 30, 30}, "Settings"},
        {K::Button, kKeyboardId, {2290, 80, 30, 30}, "Keyboard"},  // DECISION A139: beside the gear

        // Sliders (the rect is the track), Up / Down.
        {K::Slider, A(Analog::Volume), {478, 145, 62, 158}, "Volume"},
        {K::Slider, A(Analog::DataEntry), {680, 145, 62, 158}, "Data Entry"},
        {K::Label, -1, {478, 342, 62, 14}, "Volume"},
        {K::Label, -1, {680, 342, 62, 14}, "Data Entry"},
        {K::Button, B(Button::Up), {598, 208, 44, 26}, "Up"},
        {K::Button, B(Button::Down), {598, 266, 44, 26}, "Down"},

        // Display and its soft buttons (1-3 above, 4-6 below).
        {K::Display, -1, {795, 115, 607, 160}, "Display"},
        {K::Button, B(Button::Soft1), {936, 124, 44, 16}, "Soft 1"},
        {K::Button, B(Button::Soft2), {1094, 124, 44, 16}, "Soft 2"},
        {K::Button, B(Button::Soft3), {1266, 124, 44, 16}, "Soft 3"},
        {K::Button, B(Button::Soft4), {936, 250, 44, 16}, "Soft 4"},
        {K::Button, B(Button::Soft5), {1094, 250, 44, 16}, "Soft 5"},
        {K::Button, B(Button::Soft6), {1266, 250, 44, 16}, "Soft 6"},

        // Mode row, LEDs over the first three.
        {K::Button, B(Button::SeqsSongs), {800, 315, 37, 28}, "Seqs/Songs"},
        {K::Button, B(Button::Presets), {839, 315, 37, 28}, "Presets"},
        {K::Button, B(Button::Sounds), {878, 315, 37, 28}, "Sounds"},
        {K::Button, B(Button::BankSet), {917, 315, 37, 28}, "BankSet"},
        {K::Led, kSeqLedId, {813, 300, 10, 6}, "Seqs/Songs"},
        {K::Led, 1, {852, 300, 10, 6}, "Presets"},
        {K::Led, 9, {891, 300, 10, 6}, "Sounds"},
        {K::Label, -1, {800, 346, 37, 24}, "Seqs\nSongs"},
        {K::Label, -1, {839, 346, 37, 24}, "Presets"},
        {K::Label, -1, {878, 346, 37, 24}, "Sounds"},
        {K::Label, -1, {917, 346, 37, 24}, "BankSet"},

        // Bank group names.
        {K::Label, -1, {962, 284, 34, 14}, "Bank"},
        {K::Label, -1, {998, 350, 76, 12}, "User RAM BankSets", Ink::Dim},
        {K::Label, -1, {1077, 350, 236, 12}, "ROM BankSets", Ink::Dim},
        {K::Label, -1, {1314, 350, 77, 12}, "Sampled Sound BankSets", Ink::Dim},

        // Block titles and the Track Parameters / Programming rows of the fourth row.
        {K::Label, -1, {1465, 92, 140, 14}, "Track Parameters"},
        {K::Label, -1, {1765, 92, 140, 14}, "Programming"},
        {K::Label, -1, {1506, 346, 76, 14}, "Seq/Song Tracks"},
        {K::Label, -1, {1654, 346, 36, 12}, "Continue", Ink::Dim},
    };

    // Bank 0-9: button, LED and digit.
    static const Button banks[10] = {Button::Bank0, Button::Bank1, Button::Bank2, Button::Bank3, Button::Bank4,
                                     Button::Bank5, Button::Bank6, Button::Bank7, Button::Bank8, Button::Bank9};
    static const int bankLed[10] = {0, 8, 2, 10, 3, 11, 4, 12, 5, 13};  // docs/ts-hardware-interface.md
    static const char* const bankName[10] = {"Bank 0", "Bank 1", "Bank 2", "Bank 3", "Bank 4",
                                             "Bank 5", "Bank 6", "Bank 7", "Bank 8", "Bank 9"};
    static const char* const digit[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int n = 0; n < 10; ++n) {
        const int x = static_cast<int>(std::lround(998 + 39.5 * n));
        t.push_back({K::Button, B(banks[n]), {x, 315, 37, 28}, bankName[n]});
        t.push_back({K::Led, bankLed[n], {x + 13, 300, 10, 6}, bankName[n]});
        t.push_back({K::Label, -1, {x + 10, 286, 16, 14}, digit[n], Ink::Bright});
    }

    // Track Parameters and the middle block, then Programming: three rows each, then the fourth row.
    static const char* const trackLabels[9] = {"Mix\nPan", "Attack\nRelease", "Brightness\nTimbre",
                                               "Key Zone\nVelocity", "Tuning", "Controllers\nOn/Off",
                                               "Performance\nOptions", "Track\nMIDI", "Track\nEffects"};
    static const Button trackButtons[9] = {Button::MixPan, Button::AttackRelease, Button::BrightnessTimbre,
                                           Button::KeyZoneVelocity, Button::Tuning, Button::ControllersOnOff,
                                           Button::PerformanceOptions, Button::TrackMidi, Button::TrackEffects};
    block(t, 1466, 40, 3, trackLabels, trackButtons);
    static const char* const seqLabels[9] = {"System", "Storage\n(Disk)", "MIDI\nControl", "Edit\nSong",
                                             "Edit\nSequence", "Edit\nTrack", "Click", "Sequencer\nControl",
                                             "Locate"};
    static const Button seqButtons[9] = {Button::System, Button::Storage, Button::MidiControl,
                                         Button::EditSong, Button::EditSequence, Button::EditTrack,
                                         Button::Click, Button::SequencerControl, Button::Locate};
    block(t, 1618, 40, 3, seqLabels, seqButtons);
    static const char* const progLabels[12] = {"LFO", "Env 1", "Env 2", "Env 3", "Pitch", "Pitch\nMods",
                                               "Filters", "Output", "Wave", "Mod\nMixer", "Program\nControl",
                                               "Program\nEffects"};
    static const Button progButtons[12] = {Button::Lfo, Button::Env1, Button::Env2, Button::Env3, Button::Pitch,
                                           Button::PitchMods, Button::Filters, Button::Output, Button::Wave,
                                           Button::ModMixer, Button::ProgramControl, Button::ProgramEffects};
    block(t, 1766, 40, 4, progLabels, progButtons);

    // Fourth row: 36 x 26 buttons at y 316, a two-line label above and the LED over each.
    struct Row4 {
        int x;
        Button b;
        int led;  // -1: none
        const char* label;
    };
    static const Row4 row4[] = {
        {1466, Button::ReplaceTrackSound, 6, "Replace\nTrack Sound"}, {1506, Button::Tracks1to6, 7, "1-6"},
        {1546, Button::Tracks7to12, 15, "7-12"}, {1618, Button::Record, -1, "Record"},
        {1658, Button::StopContinue, -1, "Stop"}, {1698, Button::Play, -1, "Play"},
        {1766, Button::SelectVoice, -1, "Select\nVoice"}, {1806, Button::Copy, -1, "Copy"},
        {1846, Button::Write, -1, "Write\nProgram"}, {1886, Button::Compare, 14, "Compare"},
    };
    for (const Row4& r : row4) {
        t.push_back({K::Button, B(r.b), {r.x, 316, 36, 26}, r.label});
        t.push_back({K::Label, -1, {r.x - 4, 274, 44, 22}, r.label});
        if (r.led >= 0) t.push_back({K::Led, r.led, {r.x + 13, 300, 10, 6}, r.label});
    }

    // Left of the keys: the patch buttons, the wheels.
    t.push_back({K::Patch, 0, {110, 612, 33, 20}, "Patch left"});
    t.push_back({K::Patch, 1, {150, 612, 33, 20}, "Patch right"});
    t.push_back({K::Wheel, A(Analog::PitchWheel), {135, 688, 42, 195}, "Pitch wheel"});
    t.push_back({K::Wheel, A(Analog::ModWheel), {222, 688, 42, 195}, "Mod wheel"});
    t.push_back({K::Label, -1, {135, 890, 42, 14}, "Pitch"});
    t.push_back({K::Label, -1, {222, 890, 42, 14}, "Mod"});

    // Keys: whites first, so the blacks draw over them. The names outlive the table (static).
    // DECISION A137: both models draw their keys in the same kKeysW; the TS-12's 76 keys (45 white) are narrower.
    const ts::KeyRange kKeys = ts::keyRange(m);
    static char names[2][76][5];
    int whites = 0;
    for (int note = kKeys.lowest; note < kKeys.lowest + kKeys.count; ++note) whites += !isBlack(note);
    const double ww = static_cast<double>(kKeysW) / whites, bw = ww * 0.6;
    const int bh = static_cast<int>(std::lround(kKeysH * 0.62));
    std::vector<Control> blacks;
    int wi = 0;
    for (int note = kKeys.lowest; note < kKeys.lowest + kKeys.count; ++note) {
        char* name = names[static_cast<int>(m)][note - kKeys.lowest];
        const char* n = kNoteNames[note % 12];
        int len = 0;
        while (*n) name[len++] = *n++;
        name[len++] = static_cast<char>('0' + note / 12 - 1);
        name[len] = 0;
        if (isBlack(note)) {
            const int x = kKeysX + static_cast<int>(std::lround(wi * ww - bw / 2));
            blacks.push_back({K::Key, note, {x, kKeysY, static_cast<int>(std::lround(bw)), bh}, name});
        } else {
            const int x0 = static_cast<int>(std::lround(wi * ww)), x1 = static_cast<int>(std::lround((wi + 1) * ww));
            t.push_back({K::Key, note, {kKeysX + x0, kKeysY, x1 - x0, kKeysH}, name});
            ++wi;
        }
    }
    t.insert(t.end(), blacks.begin(), blacks.end());
    return t;
}

}  // namespace

const std::vector<Control>& panelLayout(ts::Model m) {
    static const std::vector<Control> tables[2] = {build(ts::Model::Ts10), build(ts::Model::Ts12)};
    return tables[static_cast<int>(m)];
}

}  // namespace ens::app
