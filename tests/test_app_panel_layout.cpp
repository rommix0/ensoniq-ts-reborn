// The panel layout table: every button, LED and key once, everything inside 2576 x 920, no overlapping
// controls, each LED over its button; and the analog map (A110).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

int countIn(const std::vector<Control>& t, ControlKind kind, int id) {
    int n = 0;
    for (const Control& c : t) n += c.kind == kind && c.id == id;
    return n;
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

TEST(panel_layout_ts12_keys) {
    using ens::ts::Model;
    const auto& t10 = panelLayout(Model::Ts10);
    const auto& t12 = panelLayout(Model::Ts12);
    int x0 = 1 << 30, y0 = 1 << 30, x1 = 0, y1 = 0;
    for (const Control& c : t10) {
        if (c.kind != ControlKind::Key) continue;
        x0 = std::min(x0, c.r.x), y0 = std::min(y0, c.r.y);
        x1 = std::max(x1, c.r.x + c.r.w), y1 = std::max(y1, c.r.y + c.r.h);
    }
    int keys = 0, whites = 0;
    std::vector<const Control*> rest12, rest10;
    for (const Control& c : t12) {
        if (c.kind != ControlKind::Key) {
            rest12.push_back(&c);
            continue;
        }
        ++keys;
        whites += !isBlack(c.id);
        CHECK(c.id >= 28 && c.id <= 103);
        CHECK(c.r.x >= x0 && c.r.y >= y0 && c.r.x + c.r.w <= x1 && c.r.y + c.r.h <= y1);
    }
    CHECK_EQ(keys, 76);
    CHECK_EQ(whites, 45);
    for (int note = 28; note <= 103; ++note) CHECK_EQ(countIn(t12, ControlKind::Key, note), 1);
    for (const Control& c : t10)
        if (c.kind != ControlKind::Key) rest10.push_back(&c);
    CHECK_EQ(rest12.size(), rest10.size());
    for (size_t i = 0; i < rest10.size() && i < rest12.size(); ++i) {
        const Control &a = *rest10[i], &b = *rest12[i];
        CHECK(a.kind == b.kind && a.id == b.id && a.ink == b.ink);
        CHECK(a.r.x == b.r.x && a.r.y == b.r.y && a.r.w == b.r.w && a.r.h == b.r.h);
        CHECK(std::string(a.label) == std::string(b.label));
    }
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

// DECISION A139: the strip (keys, wheels, patch buttons and their labels) lies below kTopH, everything else above;
// one keyboard toggle per table.
TEST(panel_layout_strip_below_top) {
    for (auto m : {ens::ts::Model::Ts10, ens::ts::Model::Ts12}) {
        int toggles = 0;
        for (const Control& c : panelLayout(m)) {
            const bool strip = c.kind == ControlKind::Key || c.kind == ControlKind::Wheel ||
                               c.kind == ControlKind::Patch || (c.kind == ControlKind::Label && c.r.y >= kTopH);
            CHECK(strip ? c.r.y >= kTopH : c.r.y + c.r.h <= kTopH);
            toggles += c.kind == ControlKind::Button && c.id == kKeyboardId;
        }
        CHECK_EQ(toggles, 1);
    }
}
