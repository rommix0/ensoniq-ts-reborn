#include "ts/panel.h"

#include <algorithm>

namespace ens::ts {

namespace {

constexpr int kPositions = kPanelRows * kPanelCols;
constexpr uint8_t kCalibrate = 0xFD, kKeyboardReady = 0xFF;  // DECISION T19

}  // namespace

void Panel::reset() {
    s_ = PanelState{};
    clearDisplay();
    toCpu_.clear();
    dropped_ = 0;
    cmd_ = 0;
    saved_ = 0;
    kres_ = sack_ = false;
}

void Panel::clearDisplay() {
    for (auto& r : s_.text) std::fill(std::begin(r), std::end(r), ' ');
    for (int r = 0; r < kPanelRows; ++r) s_.blink[r] = s_.underline[r] = 0;
    cursor_ = 0;
    keepAttributes_ = true;
}

void Panel::mode(bool underline, bool blink) {
    underlineMode_ = underline;
    blinkMode_ = blink;
    keepAttributes_ = false;
}

void Panel::cell(bool underline, bool blink) {
    const int r = cursor_ / kPanelCols;
    const uint64_t bit = 1ull << (cursor_ % kPanelCols);
    s_.underline[r] = underline ? (s_.underline[r] | bit) : (s_.underline[r] & ~bit);
    s_.blink[r] = blink ? (s_.blink[r] | bit) : (s_.blink[r] & ~bit);
}

// A character at the cursor; it takes the attributes of the current mode, or keeps the cell's own
// after 0xE8. The cursor moves on, from the end of row 0 into row 1. DECISION T18: past position 79
// it wraps to 0.
void Panel::put(uint8_t c) {
    s_.text[cursor_ / kPanelCols][cursor_ % kPanelCols] = static_cast<char>(c);
    if (!keepAttributes_) cell(underlineMode_, blinkMode_);
    cursor_ = static_cast<uint8_t>((cursor_ + 1) % kPositions);
}

// DECISION T18: the command set as decoded from the OS v3.10 stream (docs/ts-hardware-interface.md,
// "Display and LEDs"). Bytes outside it are counted in `unknown` and take no parameter.
void Panel::fromCpu(uint8_t b) {
    if (cmd_) {  // the parameter byte, whatever its value
        const uint8_t c = cmd_;
        cmd_ = 0;
        run(c, b);
        return;
    }
    if (b < 0x7F) {
        put(b);
        return;
    }
    if (b >= 0x80 && b < 0x80 + kPositions) {  // cursor to position b - 0x80
        cursor_ = static_cast<uint8_t>(b - 0x80);
        return;
    }
    switch (b) {
    case 0xD0: mode(false, true); break;   // characters written from here on flash
    case 0xD1: mode(false, false); break;  // ... are plain
    case 0xD2: mode(true, true); break;    // ... have a flashing underline
    case 0xD3: mode(true, false); break;   // ... are underlined
    case 0xE8: keepAttributes_ = true; break;  // ... keep the attributes their cells have
    case 0x7F: case 0xE7: break;  // no visible effect: 0x7F comes before positioning and modes, 0xE7
                                  // before every keyboard start-up (boot, disk access)
    case 0xD4: cursor_ = static_cast<uint8_t>((cursor_ + 1) % kPositions); break;              // cursor right
    case 0xD5: cursor_ = static_cast<uint8_t>((cursor_ + kPositions - 1) % kPositions); break;  // cursor left
    case 0xD6: clearDisplay(); break;
    case 0xD9: cell(true, false); break;   // underline the cell at the cursor
    case 0xDB: cell(false, false); break;  // and remove it
    case 0xF5: saved_ = cursor_; break;
    case 0xF6: cursor_ = saved_; break;
    case 0xFB: case 0xFF: cmd_ = b; break;  // one parameter byte
    default: ++s_.unknown; break;
    }
}

void Panel::run(uint8_t cmd, uint8_t param) {
    if (cmd == 0xFF) {  // LED: bits 3-0 the number, bit 7 lit, bit 6 flashing
        const uint32_t n = param & 0x0F;
        const uint32_t on = 1u << n, flash = 1u << (16 + n);
        s_.leds = (param & 0x80) ? (s_.leds | on) : (s_.leds & ~on);
        s_.leds = (param & 0x40) ? (s_.leds | flash) : (s_.leds & ~flash);
        return;
    }
    // 0xFB: a command for the keyboard. DECISION T19: it answers FF to FD (the OS's start-up check)
    // and ignores the others it is sent (FF, F0, DD, FC). A keyboard held in reset hears nothing.
    if (param == kCalibrate && !kres_) toCpu_.push(kKeyboardReady);
}

bool Panel::popToCpu(uint8_t& b) {
    if (kres_ || sack_) return false;  // DECISION T19: the keyboard sends only while SACK/ is low
    return toCpu_.pop(b);
}

// DECISION T19: KRES (OP4) and DRES (OP5) are active-high resets: the OS holds them high from the
// DUART's reset until 0.24 ms. KRES empties the keyboard's queue; DRES blanks the display and the LEDs.
void Panel::setLines(bool kres, bool dres, bool sack) {
    kres_ = kres;
    sack_ = sack;
    if (kres) toCpu_.clear();
    if (dres) {
        clearDisplay();
        s_.leds = 0;
        cmd_ = 0;
    }
}

// An event is two bytes: when the ring has no room for both it is dropped whole, so the CPU never sees
// half of one.
void Panel::queue(uint8_t a, uint8_t b) {
    if (kres_) return;
    if (toCpu_.size() + 2 > kToCpuSize) {
        ++dropped_;
        return;
    }
    toCpu_.push(a);
    toCpu_.push(b);
}

// DECISION T16: a press is code | 0x80 then 0x00, a release code then 0x00.
void Panel::button(Button b, bool down) {
    const auto code = static_cast<uint8_t>(b);
    queue(down ? static_cast<uint8_t>(code | 0x80) : code, 0x00);
}

// DECISION T17: key down is 0x80 | k then the strike value (nonzero: 0 would make it a button
// press), key up k then a nonzero value (the OS sends note-off velocity 64 whatever it is).
void Panel::key(int note, bool down, int velocity) {
    if (note < kLowestKey || note >= kLowestKey + kKeyCount) return;
    const auto k = static_cast<uint8_t>(note - kLowestKey);
    if (down) queue(static_cast<uint8_t>(0x80 | k), static_cast<uint8_t>(std::clamp(velocity, 1, 127)));
    else queue(k, 0x40);
}

// DECISION T17: Poly-Key pressure is 0x40 | k then the amount (0-127).
void Panel::pressure(int note, int amount) {
    if (note < kLowestKey || note >= kLowestKey + kKeyCount) return;
    queue(static_cast<uint8_t>(0x40 | (note - kLowestKey)), static_cast<uint8_t>(std::clamp(amount, 0, 127)));
}

}  // namespace ens::ts
