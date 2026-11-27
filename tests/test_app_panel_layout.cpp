// The panel layout table: every button, LED and key once, everything inside 2576 x 920, no overlapping
// controls, each LED over its button; and the analog map (A110).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/analog_map.h"
#include "app/panel_layout.h"
#include "check.h"
#include "ts/panel.h"

using namespace ens::app;
using ens::ts::Analog;
using ens::ts::Button;
using ens::ts::kAnalogRest;

namespace {

int count(ControlKind kind, int id) {
    int n = 0;
    for (const Control& c : panelLayout()) n += c.kind == kind && c.id == id;
    return n;
}

const Control* find(ControlKind kind, int id) {
    for (const Control& c : panelLayout())
        if (c.kind == kind && c.id == id) return &c;
    return nullptr;
}

bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool isBlack(int note) {
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

int centreDist(const Rect& a, const Rect& b) {
    const int dx = std::abs(a.x + a.w / 2 - (b.x + b.w / 2)), dy = std::abs(a.y + a.h / 2 - (b.y + b.h / 2));
    return std::max(dx, dy);
}

}  // namespace

TEST(panel_layout_has_every_button) {
    for (const auto& b : ens::ts::kButtons) {
        if (b.button == Button::FootSwitch1Left || b.button == Button::FootSwitch1Right) continue;
        const int n = count(ControlKind::Button, static_cast<int>(b.button));
        if (n != 1) std::printf("    %s appears %d times\n", b.name, n);
        CHECK_EQ(n, 1);
    }
    CHECK_EQ(count(ControlKind::Button, kSettingsId), 1);
}

TEST(panel_layout_has_every_led_and_key) {
    for (int led = 0; led < ens::ts::kPanelLeds; ++led) CHECK_EQ(count(ControlKind::Led, led), 1);
    CHECK_EQ(count(ControlKind::Led, kSeqLedId), 1);
    constexpr ens::ts::KeyRange kKeys = ens::ts::keyRange(ens::ts::Model::Ts10);
    for (int note = kKeys.lowest; note < kKeys.lowest + kKeys.count; ++note)
        CHECK_EQ(count(ControlKind::Key, note), 1);
    int keys = 0, displays = 0;
    for (const Control& c : panelLayout()) {
        keys += c.kind == ControlKind::Key;
        displays += c.kind == ControlKind::Display;
    }
    CHECK_EQ(keys, kKeys.count);
    CHECK_EQ(displays, 1);
    CHECK_EQ(count(ControlKind::Patch, 0), 1);
    CHECK_EQ(count(ControlKind::Patch, 1), 1);
    CHECK_EQ(count(ControlKind::Slider, static_cast<int>(Analog::Volume)), 1);
    CHECK_EQ(count(ControlKind::Slider, static_cast<int>(Analog::DataEntry)), 1);
    CHECK_EQ(count(ControlKind::Wheel, static_cast<int>(Analog::PitchWheel)), 1);
    CHECK_EQ(count(ControlKind::Wheel, static_cast<int>(Analog::ModWheel)), 1);
}

TEST(panel_layout_fits_and_does_not_overlap) {
    const auto& t = panelLayout();
    CHECK(!t.empty());
    for (const Control& c : t) {
        CHECK(c.r.w > 0 && c.r.h > 0);
        CHECK(c.r.x >= 0 && c.r.y >= 0 && c.r.x + c.r.w <= kDesignW && c.r.y + c.r.h <= kDesignH);
    }
    auto solid = [](const Control& c) {
        return c.kind == ControlKind::Button || c.kind == ControlKind::Slider || c.kind == ControlKind::Wheel ||
               c.kind == ControlKind::Patch || c.kind == ControlKind::Key;
    };
    for (size_t i = 0; i < t.size(); ++i) {
        for (size_t j = i + 1; j < t.size(); ++j) {
            const Control &a = t[i], &b = t[j];
            if (!solid(a) || !solid(b) || !overlap(a.r, b.r)) continue;
            // A black key may cover the white keys beside it.
            if (a.kind == ControlKind::Key && b.kind == ControlKind::Key && isBlack(a.id) != isBlack(b.id)) continue;
            std::printf("    overlap: %s and %s\n", a.label, b.label);
            CHECK(false);
        }
    }
}

// The decals (A122) sit on bare panel: no control under the logo, its line or the badge.
TEST(panel_layout_decals_are_clear) {
    int decals = 0;
    for (const Control& d : panelLayout()) {
        if (d.ink != Ink::Decal) continue;
        ++decals;
        for (const Control& c : panelLayout())
            if (c.kind != ControlKind::Label && overlap(d.r, c.r)) {
                std::printf("    %s under %s\n", c.label, d.label);
                CHECK(false);
            }
    }
    CHECK_EQ(decals, 3);
}

TEST(panel_layout_led_matches_button) {
    // docs/ts-hardware-interface.md, "Display and LEDs".
    const Button byLed[ens::ts::kPanelLeds] = {
        Button::Bank0, Button::Presets, Button::Bank2, Button::Bank4, Button::Bank6, Button::Bank8,
        Button::ReplaceTrackSound, Button::Tracks1to6, Button::Bank1, Button::Sounds, Button::Bank3,
        Button::Bank5, Button::Bank7, Button::Bank9, Button::Compare, Button::Tracks7to12,
    };
    auto check = [](int led, Button button) {
        const Control* l = find(ControlKind::Led, led);
        const Control* b = find(ControlKind::Button, static_cast<int>(button));
        CHECK(l != nullptr && b != nullptr);
        if (l == nullptr || b == nullptr) return;
        if (centreDist(l->r, b->r) > 40) std::printf("    LED %d is %d from %s\n", led, centreDist(l->r, b->r), b->label);
        CHECK(centreDist(l->r, b->r) <= 40);
    };
    for (int led = 0; led < ens::ts::kPanelLeds; ++led) check(led, byLed[led]);
    check(kSeqLedId, Button::SeqsSongs);
}

TEST(analog_map_round_trips_rest_values) {
    for (const Analog a : {Analog::PitchWheel, Analog::ModWheel, Analog::Volume, Analog::CvPedal, Analog::DataEntry}) {
        const uint16_t rest = kAnalogRest[static_cast<size_t>(a)];
        CHECK(analogRaw(a, analogPosition(a, rest)) == rest);
    }
    CHECK(analogRaw(Analog::PitchWheel, 0.5f) == 0x200);
    CHECK(analogRaw(Analog::Volume, 1.0f) == 0x3FF);
    CHECK(analogRaw(Analog::ModWheel, 0.0f) == 0);
}

TEST(analog_map_data_entry_never_zero) {
    CHECK(analogRaw(Analog::DataEntry, 0.0f) == 1);
    CHECK(analogRaw(Analog::DataEntry, -1.0f) == 1);
    CHECK(analogRaw(Analog::DataEntry, -INFINITY) == 1);
    CHECK(analogRaw(Analog::DataEntry, 1.0f) == 0x3FF);
    CHECK(analogRaw(Analog::Volume, 0.0f) == 0);
}
