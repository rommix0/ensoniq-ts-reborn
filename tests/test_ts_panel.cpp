// The TS-10 panel and keyboard model fed with the byte sequences decoded from the OS v3.10 stream
// (docs/ts-hardware-interface.md, "Display and LEDs", "Keyboard link", "Buttons", "Keys and pressure").
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "check.h"
#include "ts/panel.h"

using namespace ens::ts;

namespace {

void feed(Panel& p, std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) p.fromCpu(b);
}

void feed(Panel& p, const std::string& text) {
    for (char c : text) p.fromCpu(static_cast<uint8_t>(c));
}

// Row r with `text` at column `col` and blanks elsewhere.
std::string at(int col, const std::string& text) {
    std::string row(kPanelCols, ' ');
    row.replace(static_cast<size_t>(col), text.size(), text);
    return row;
}

// Bits first..last of a row mask.
uint64_t bits(int first, int last) {
    uint64_t m = 0;
    for (int c = first; c <= last; ++c) m |= 1ull << c;
    return m;
}

std::vector<uint8_t> toCpu(Panel& p) {
    std::vector<uint8_t> out;
    for (uint8_t b = 0; p.popToCpu(b);) out.push_back(b);
    return out;
}

const std::string kBlank(kPanelCols, ' ');

}  // namespace

TEST(ts_panel_reset_state) {
    Panel p;
    CHECK(rowText(p.state(), 0) == kBlank);
    CHECK(rowText(p.state(), 1) == kBlank);
    CHECK_EQ(p.state().leds, 0u);
    CHECK_EQ(p.state().unknown, 0u);
    CHECK(toCpu(p).empty());
}

// One case per decoded command, each with the bytes the OS sends for it.
TEST(ts_panel_commands) {
    {  // 0x00-0x7E: a character at the cursor; the cursor moves on, from column 39 of row 0 into row 1
        Panel p;
        feed(p, {0xA7});
        feed(p, "xy");
        CHECK(rowText(p.state(), 0) == at(39, "x"));
        CHECK(rowText(p.state(), 1) == at(0, "y"));
    }
    {  // 0x80 + p: cursor to position p (row p / 40): "ENSONIQ  TS-10" centred, then row 1 column 3
        Panel p;
        feed(p, {0xD6, 0x7F, 0x8D});
        feed(p, "ENSONIQ  TS-10");
        feed(p, {0x7F, 0xAB});
        feed(p, "PERFORMANCE");
        CHECK(rowText(p.state(), 0) == at(13, "ENSONIQ  TS-10"));
        CHECK(rowText(p.state(), 1) == at(3, "PERFORMANCE"));
    }
    {  // 0xD6: blank the display and its attributes, cursor home (text follows with no position)
        Panel p;
        feed(p, {0x88, 0xD3});
        feed(p, "TUNE");
        feed(p, {0xE8, 0xD6});
        feed(p, "U0-0");
        CHECK(rowText(p.state(), 0) == at(0, "U0-0"));
        CHECK_EQ(p.state().underline[0], 0u);
    }
    {  // 0xD3: underlined characters, 0xD1 plain ones, 0x7F no effect: the selected sound of a bank page
        Panel p;
        feed(p, {0x85, 0xD1, 0xD1});
        feed(p, "BABY-GRAND ");
        feed(p, {0xE8, 0x91, 0xD1, 0xD1, 0xD3});
        feed(p, " FULL-BODY ");
        feed(p, {0xE8, 0x9D, 0xD1, 0xD1, 0x7F});
        feed(p, " BRITE-PNO ");
        feed(p, {0xE8});
        CHECK(rowText(p.state(), 0) == at(5, "BABY-GRAND   FULL-BODY   BRITE-PNO "));
        CHECK_EQ(p.state().underline[0], bits(17, 27));
        CHECK_EQ(p.state().blink[0], 0u);
    }
    {  // 0xD2: flashing underline (a layered sound)
        Panel p;
        feed(p, {0x9D, 0xD1, 0xD1, 0xD2});
        feed(p, " BRITE-PNO ");
        feed(p, {0xE8});
        CHECK_EQ(p.state().underline[0], bits(29, 39));
        CHECK_EQ(p.state().blink[0], bits(29, 39));
    }
    {  // 0xD0: flashing characters ("SAVE" on the Write page), ended by 0xD1
        Panel p;
        feed(p, {0x7F, 0xD0, 0xA8});
        feed(p, "SAVE");
        feed(p, {0xD1});
        feed(p, "x");
        CHECK(rowText(p.state(), 1) == at(0, "SAVEx"));
        CHECK_EQ(p.state().blink[1], bits(0, 3));
        CHECK_EQ(p.state().underline[1], 0u);
    }
    {  // 0xF5 / 0xF6: save the cursor and go back to it (a value redrawn in place)
        Panel p;
        feed(p, {0x88});
        feed(p, "TUNE");
        feed(p, {0xD1});
        feed(p, "=");
        feed(p, {0xF5, 0xD3});
        feed(p, "+00");
        feed(p, {0xE8, 0x9A, 0xD1});
        feed(p, "PITCH-BEND=02 ");
        feed(p, {0xE8, 0xF6, 0xD3});
        feed(p, "-99");
        feed(p, {0xE8});
        CHECK(rowText(p.state(), 0) == at(8, "TUNE=-99          PITCH-BEND=02 "));
        CHECK_EQ(p.state().underline[0], bits(13, 15));
        // Another parameter selected: the value is redrawn in 0xD1 mode, which removes the underline.
        feed(p, {0x88});
        feed(p, "TUNE");
        feed(p, {0xD1});
        feed(p, "=-99");
        feed(p, {0xE8});
        CHECK_EQ(p.state().underline[0], 0u);
    }
    {  // 0xD5 / 0xD4: cursor left / right by one (a 12-column track field starting one left of 0x85)
        Panel p;
        feed(p, {0x85, 0xD1, 0xD5});
        feed(p, "*");
        feed(p, {0xD4});
        feed(p, "x");
        CHECK(rowText(p.state(), 0) == at(4, "* x"));
    }
    {  // 0xD9 / 0xDB: underline the cell at the cursor / remove it, the cursor staying; after 0xE8 a
       // character keeps its cell's underline (the name editor's cursor under a letter it redraws)
        Panel p;
        feed(p, {0x9A, 0xD9, 0xE8, 0x9A});
        feed(p, "B");
        CHECK_EQ(p.state().underline[0], bits(26, 26));
        CHECK(rowText(p.state(), 0) == at(26, "B"));
        feed(p, {0x9A, 0xDB});
        CHECK_EQ(p.state().underline[0], 0u);
    }
    {  // 0xFF n: LED n & 15 lit (bit 7) and flashing (bit 6); the OS refreshes all 16, 0x0F first
        Panel p;
        feed(p, {0xFF, 0x89, 0xFF, 0x80, 0xFF, 0xC6});
        CHECK_EQ(p.state().leds, (1u << 9) | (1u << 0) | (1u << 6) | (1u << (16 + 6)));
        feed(p, {0xFF, 0x09, 0xFF, 0x86});
        CHECK_EQ(p.state().leds, (1u << 0) | (1u << 6));
        CHECK(rowText(p.state(), 0) == kBlank);  // parameters are not characters
    }
    {  // 0xFB n: a keyboard command. FD (the start-up check) is answered with FF; F0, FF, DD, FC are not
        Panel p;
        feed(p, {0xFB, 0xFF, 0xFB, 0xF0, 0xFB, 0xDD, 0xFB, 0xFC});
        CHECK(toCpu(p).empty());
        feed(p, {0xFB, 0xFD});
        CHECK(toCpu(p) == std::vector<uint8_t>{0xFF});
        CHECK(rowText(p.state(), 0) == kBlank);
    }
    {  // 0xE7: sent before every keyboard start-up (boot, disk access); nothing visible
        Panel p;
        feed(p, {0xE7});
        feed(p, "a");
        CHECK(rowText(p.state(), 0) == at(0, "a"));
        CHECK_EQ(p.state().unknown, 0u);
    }
    {  // anything else of 0x80 and up is counted and takes no parameter
        Panel p;
        feed(p, {0xE0, 0xFE});
        feed(p, "a");
        CHECK_EQ(p.state().unknown, 2u);
        CHECK(rowText(p.state(), 0) == at(0, "a"));
    }
}

// What the keyboard sends: buttons, keys and Poly-Key pressure (T16, T17).
TEST(ts_panel_button_bytes) {
    Panel p;
    p.button(Button::System, true);
    p.button(Button::System, false);
    CHECK(toCpu(p) == (std::vector<uint8_t>{0x8A, 0x00, 0x0A, 0x00}));
    p.button(Button::Bank0, true);
    p.button(Button::Compare, true);
    CHECK(toCpu(p) == (std::vector<uint8_t>{0x86, 0x00, 0xBD, 0x00}));
    p.key(60, true, 100);
    p.key(60, false, 0);
    CHECK(toCpu(p) == (std::vector<uint8_t>{0x98, 100, 0x18, 0x40}));
    p.key(36, true, 0);    // a strike of 0 would read as a button: clamped to 1
    p.key(96, true, 200);  // the top key; clamped to 127
    CHECK(toCpu(p) == (std::vector<uint8_t>{0x80, 1, 0xBC, 127}));
    p.key(35, true, 64);  // outside C2-C7: ignored
    p.key(97, true, 64);
    p.pressure(97, 10);
    CHECK(toCpu(p).empty());
    p.pressure(64, 0);
    p.pressure(64, 300);
    CHECK(toCpu(p) == (std::vector<uint8_t>{0x5C, 0, 0x5C, 127}));
}

// Every name maps to a different code, and the 62 buttons and 2 foot switch contacts fill the 6-bit codes.
TEST(ts_panel_button_table) {
    uint64_t seen = 0;
    for (const ButtonName& b : kButtons) {
        const auto code = static_cast<uint8_t>(b.button);
        CHECK(code < 0x40);
        CHECK(!(seen >> code & 1));
        seen |= 1ull << code;
    }
    CHECK_EQ(seen, ~0ull);
}

// A full keyboard queue drops whole events, never half of one.
TEST(ts_panel_full_queue_drops_whole_events) {
    Panel p;
    for (size_t i = 0; i < Panel::kToCpuSize / 2 + 3; ++i) p.button(Button::Up, (i & 1) == 0);
    CHECK_EQ(p.toCpuQueued(), Panel::kToCpuSize);
    CHECK_EQ(p.dropped(), 3u);
    const std::vector<uint8_t> out = toCpu(p);
    for (size_t i = 0; i < out.size(); i += 2) CHECK_EQ(out[i + 1], 0);
}

// KRES (OP4) and DRES (OP5) are active-high resets; SACK/ (OP7) high holds the keyboard (T19).
TEST(ts_panel_lines) {
    Panel p;
    p.button(Button::Play, true);
    p.setLines(false, false, true);  // SACK/ high: nothing goes out, nothing is lost
    uint8_t b = 0;
    CHECK(!p.popToCpu(b));
    p.setLines(false, false, false);
    CHECK(toCpu(p) == (std::vector<uint8_t>{0x9D, 0x00}));
    p.button(Button::Play, true);
    p.setLines(true, false, false);  // KRES: the keyboard's queue is emptied and it hears nothing
    p.fromCpu(0xFB);
    p.fromCpu(0xFD);
    p.button(Button::Play, false);
    p.setLines(false, false, false);
    CHECK(toCpu(p).empty());
    feed(p, {0xFF, 0x89});
    feed(p, "abc");
    p.setLines(false, true, false);  // DRES: display and LEDs blank
    CHECK(rowText(p.state(), 0) == kBlank);
    CHECK_EQ(p.state().leds, 0u);
}
