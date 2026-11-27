#pragma once
// The TS-10 and TS-12 front panel (one table; the TS-12 differs in its keys, A137) in 2576 x 920 design units,
// measured on datasheets/ts10-photo.webp scaled to that size (arrangement and proportions) with the labels of the
// Musician's Manual page 3.
// JUCE-free; the editor scales it uniformly to the window.
#include <cstdint>
#include <vector>

#include "ts/panel.h"

namespace ens::app {

// DECISION A112: the photo scaled to 2576 x 920 design units (default window 1546 x 552, minimum 1030 x 368,
// aspect fixed).
constexpr int kDesignW = 2576, kDesignH = 920;

struct Rect {
    int x, y, w, h;
};

enum class ControlKind : uint8_t { Button, Led, Slider, Wheel, Patch, Display, Key, Label, Decor };
// Label colour and weight; Decal is the model decal.
enum class Ink : uint8_t { Text, Bright, Dim, Decal };

struct Control {
    ControlKind kind;
    // Button: ts::Button code, or kSettingsId (the gear). Led: 0-15, or kSeqLedId. Slider, Wheel: ts::Analog.
    // Patch: 0 left, 1 right. Key: a note in ts::keyRange(model). Display, Label, Decor: -1.
    int id;
    Rect r;             // Label: its text box
    // Label: the text; the others: the name for tooltips. Either may hold a line break (a space in a tooltip).
    const char* label;
    Ink ink = Ink::Text;
};

constexpr int kSettingsId = -4;  // the gear button that opens the settings page
constexpr int kKeyboardId = -5;  // DECISION A139: the button that slides the keyboard strip out and in

// DECISION A139: the top panel is y 0 to kTopH, 20 units under its lowest label (y 370, no empty band); the strip
// below it (the divider line, keys, wheels, patch buttons) slides up under it.
constexpr int kTopH = 390;
constexpr int kSeqLedId = 16;    // the Seqs/Songs LED (DUART OP3, not on the display link)

// Where the 2 x 40 character cells go, inside the display window.
constexpr Rect kDisplayText = {820, 170, 565, 58};

// Two tables (TS-10, TS-12), each built once, on first call (message thread).
const std::vector<Control>& panelLayout(ts::Model m = ts::Model::Ts10);

}  // namespace ens::app
