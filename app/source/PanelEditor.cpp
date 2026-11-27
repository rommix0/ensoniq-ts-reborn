#include "PanelEditor.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "AppSettings.h"
#include "Processor.h"
#include "SettingsPage.h"
#include "TsArt.h"
#include "app/panel_layout.h"
#include "ts/machine.h"
#include "ts/panel.h"

using ens::app::Control;
using ens::app::ControlKind;
using ens::app::Ink;

namespace {

// The panel's colours: the panel, buttons, labels, the photo's teal display, the LEDs and the decal from
// the Plan 4 Task 6 brief; the rest (keys, wheels, grooves) as KS-32 Reborn's.
const juce::Colour kPanel{0xff1c1d20}, kButton{0xff2b2c30}, kButtonEdge{0xff45474c}, kText{0xffc9cbcf},
    kVfd{0xff7fe8d6}, kVfdBack{0xff050807}, kLedOn{0xffff4a3d}, kLedOff{0xff4a2522};
// DECISION A122: the decals' colours, measured on datasheets/ts10-photo.webp: the logo's line and the badge.
// The logo's own colours are in its SVG.
const juce::Colour kDecal{0xff97938f}, kBadge{0xffa9a7a7};
// DECISION A123: a label's font height over its line's height (two-line button labels, one-line labels),
// the spacing of a two-line label's lines in lines, and the part of a box's width a line may fill.
constexpr float kLabelGrow = 1.3f, kHeadGrow = 1.12f, kLineGap = 1.12f, kLabelRoom = 0.8f;

// An SVG from the binary data; one colour for every black fill when `ink` is given (the Ensoniq badge's SVG
// is black and #231f20).
std::unique_ptr<juce::Drawable> art(const char* data, int size, juce::Colour ink = {}) {
    auto d = juce::Drawable::createFromImageData(data, static_cast<size_t>(size));
    if (d != nullptr && !ink.isTransparent()) {
        d->replaceColour(juce::Colours::black, ink);
        d->replaceColour(juce::Colour(0xff231f20), ink);
    }
    return d;
}
const juce::Colour kBack{0xff0d0d0e}, kGroove{0xff08080a}, kEdge{0xff2a2b2e}, kButtonShade{0xff1f2023},
    kButtonDown{0xff242528}, kBright{0xffe6e7ea}, kDim{0xffa7aab0}, kFocus{0xff9fd4c7}, kTrackEdge{0xff2c2e32},
    kCap{0xff4a4c51}, kCapEdge{0xff6a6d73}, kWhite{0xffecebe6}, kWhiteEdge{0xff9a9993}, kBlack{0xff1a1a1c},
    kBlackShade{0xff2e2f33}, kRidge{0xff2b2c30}, kRidgeLine{0xff38393e}, kWheelEdge{0xff4a4c51};

juce::Rectangle<int> rect(const ens::app::Rect& r) { return {r.x, r.y, r.w, r.h}; }

// The table's names carry "\n" where the photo breaks a line; a tooltip or accessible name shows a space.
juce::String oneLine(const char* s) { return juce::String(s).replaceCharacter('\n', ' '); }

void box(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour edge, float radius,
         float edgeW = 1.0f) {
    g.setColour(fill);
    g.fillRoundedRectangle(r, radius);
    g.setColour(edge);
    g.drawRoundedRectangle(r.reduced(edgeW / 2), radius, edgeW);
}

// A raised button: a bottom shade, pressed buttons drawn sunk.
void drawButton(juce::Graphics& g, juce::Rectangle<float> r, bool down) {
    g.setColour(down ? kButtonDown : kButton);
    g.fillRoundedRectangle(r, 3.0f);
    if (!down) {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(r.withTop(r.getBottom() - 3).toNearestInt());
        g.setColour(kButtonShade);
        g.fillRoundedRectangle(r.reduced(1), 2.0f);
    }
    g.setColour(down ? kButtonShade : kButtonEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
}

void drawGear(juce::Graphics& g, juce::Rectangle<float> r) {
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.26f;
    juce::Path p;
    for (int i = 0; i < 8; ++i) {
        juce::Path tooth;
        tooth.addRectangle(c.x - rad * 0.28f, c.y - rad * 1.35f, rad * 0.56f, rad * 0.8f);
        p.addPath(tooth, juce::AffineTransform::rotation(juce::MathConstants<float>::pi * i / 4, c.x, c.y));
    }
    p.addEllipse(c.x - rad, c.y - rad, rad * 2, rad * 2);
    g.setColour(kText);
    g.fillPath(p);
    g.setColour(kButton);
    g.fillEllipse(c.x - rad * 0.42f, c.y - rad * 0.42f, rad * 0.84f, rad * 0.84f);
}

// DECISION A139: the keyboard toggle: a small keyboard with a chevron above it, pointing down while the strip shows
// (a click hides it) and up while it is out.
void drawKeyboardToggle(juce::Graphics& g, juce::Rectangle<float> r, bool shown) {
    const auto k = r.reduced(r.getWidth() * 0.2f, 0).withTrimmedTop(r.getHeight() * 0.5f).withTrimmedBottom(
        r.getHeight() * 0.18f);
    g.setColour(kText);
    g.drawRect(k, 1.0f);
    for (int i = 1; i < 4; ++i) {
        const float x = k.getX() + k.getWidth() * static_cast<float>(i) / 4;
        g.drawLine(x, k.getY(), x, k.getBottom(), 1.0f);
    }
    const float cx = r.getCentreX(), w = r.getWidth() * 0.2f, top = r.getY() + r.getHeight() * 0.18f,
                h = r.getHeight() * 0.2f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - w, shown ? top : top + h);
    chevron.lineTo(cx, shown ? top + h : top);
    chevron.lineTo(cx + w, shown ? top : top + h);
    g.strokePath(chevron, juce::PathStrokeType(2.0f));
}

juce::Font labelFont(float height, bool bold) {
    return juce::Font(juce::FontOptions("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont(float height) {
    // Monospaced so every character fits its cell: drawText drops a glyph wider than the cell, which a
    // proportional fallback for a missing Consolas does to capitals (as KS-32 A7).
#if JUCE_WINDOWS
    static const juce::String mono("Consolas");
#else
    static const juce::String mono(juce::Font::getDefaultMonospacedFontName());
#endif
    return juce::Font(juce::FontOptions(mono, height, juce::Font::plain));
}

// One control of the table as a component, in design units: tooltip, accessible name, role.
class Part : public juce::Component, public juce::SettableTooltipClient {
public:
    Part(PanelEditor& ed, const Control& c) : ed_(ed), c_(c) {
        setTitle(oneLine(c.label));
        setTooltip(getTitle());
    }

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        using R = juce::AccessibilityRole;
        const auto k = c_.kind;
        const bool pressed = k == ControlKind::Button || k == ControlKind::Patch || k == ControlKind::Key ||
                             k == ControlKind::Decor;  // Decor: the disk drive (A119)
        const R role = pressed                                                                     ? R::button
                       : k == ControlKind::Slider || k == ControlKind::Wheel                       ? R::slider
                       : k == ControlKind::Display                                                 ? R::staticText
                                                                                                   : R::image;
        return std::make_unique<juce::AccessibilityHandler>(*this, role, actions());
    }
    virtual juce::AccessibilityActions actions() { return {}; }

    PanelEditor& ed_;
    const Control& c_;
};

// A panel button (ts::Button code), a patch select button (PatchSelect 0/1) or the gear.
class ButtonPart : public Part {
public:
    ButtonPart(PanelEditor& ed, const Control& c)
        : Part(ed, c), gear_(c.id == ens::app::kSettingsId), keyboard_(c.id == ens::app::kKeyboardId),
          local_(c.kind == ControlKind::Button && c.id < 0),
          kind_(c.kind == ControlKind::Patch ? ens::app::Event::Kind::PatchSelect : ens::app::Event::Kind::Button) {
        setWantsKeyboardFocus(true);
        setBounds(rect(c.r));
    }

    // DECISION A114: a right-click or Shift-click latches the button down until it is clicked again; a
    // plain press is sent on mouse-down and its release on mouse-up, wherever the mouse is (as KS-32 A9).
    void mouseDown(const juce::MouseEvent& e) override {
        if (local_) return;
        if (latched_) {
            latched_ = false;
            set(false);
        } else if (e.mods.isPopupMenu() || e.mods.isShiftDown()) {
            latched_ = true;
            set(true);
        } else {
            byMouse_ = true;
            set(true);
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (local_) {
            if (getLocalBounds().contains(e.getPosition())) act();
        } else if (byMouse_) {
            byMouse_ = false;
            if (!latched_) set(false);
        }
    }
    // Space presses the focused button while it is held.
    bool keyPressed(const juce::KeyPress& k) override {
        if (k != juce::KeyPress::spaceKey) return false;
        if (local_) {
            act();
        } else if (!want_) {
            byKey_ = true;
            set(true);
        }
        return true;
    }
    bool keyStateChanged(bool) override {
        if (!byKey_ || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey)) return false;
        byKey_ = false;
        if (!latched_) set(false);
        return true;
    }
    void focusLost(FocusChangeType) override {
        if (byKey_) release(false);
        repaint();
    }
    void focusGained(FocusChangeType) override { repaint(); }
    // DECISION A114: losing focus releases unlatched buttons. Closing the editor releases them all.
    void release(bool latchedToo) {
        byMouse_ = byKey_ = false;
        if (latchedToo) latched_ = false;
        if (!latched_) set(false);
    }
    // Sends the wanted state until the engine takes it: a full queue (A102) delays a press or a
    // release (the timer retries), never loses it.
    void sync() {
        if (local_ || want_ == down_ || !ed_.post({kind_, static_cast<uint8_t>(c_.id), want_, 0})) return;
        down_ = want_;
        repaint();
    }
    // A new engine: nothing is held or latched, and the new engine is not told.
    void reset() {
        want_ = down_ = latched_ = byMouse_ = byKey_ = false;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        // The text is the label's above the button (the fourth row's too), so a button draws none.
        const auto r = getLocalBounds().toFloat();
        drawButton(g, r, down_);
        if (gear_) drawGear(g, r);
        if (keyboard_) drawKeyboardToggle(g, r, ed_.keyboardShown());
        if (hasKeyboardFocus(false)) {
            g.setColour(kFocus);
            g.drawRoundedRectangle(r.reduced(1.0f), 3.0f, 2.0f);
        }
    }

private:
    juce::AccessibilityActions actions() override {
        return juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] {
            if (local_) {
                act();
            } else if (!want_) {
                set(true);
                set(false);
            }
        });
    }
    void set(bool down) {
        want_ = down;
        sync();
    }
    // The gear and the keyboard toggle (A139) act in the editor; they are never posted to the engine.
    void act() {
        if (gear_) ed_.openSettings();
        if (keyboard_) {
            ed_.toggleKeyboard();
            repaint();  // the chevron (no resize repaints it on a reversal mid-slide; review fix)
        }
    }

public:
    // DECISION A139: the toggle's tooltip follows the state it would change to.
    juce::String getTooltip() override {
        return keyboard_ ? juce::String(ed_.keyboardShown() ? "Hide keyboard" : "Show keyboard")
                         : SettableTooltipClient::getTooltip();
    }

private:
    const bool gear_, keyboard_, local_;
    const ens::app::Event::Kind kind_;
    // want_: what the mouse and keys ask for; down_: what the engine has been sent.
    bool want_ = false, down_ = false, latched_ = false, byMouse_ = false, byKey_ = false;
};

class LedPart : public Part {
public:
    LedPart(PanelEditor& ed, const Control& c) : Part(ed, c) {
        setTitle(getTitle() + " LED");
        setTooltip(getTitle());
        setBounds(rect(c.r).expanded(4));  // room for the glow
    }
    void paint(juce::Graphics& g) override {
        const auto& s = ed_.snap();
        bool on = s.seqLed;
        if (c_.id != ens::app::kSeqLedId) {
            // DECISION A113: bit 16 + n flashing (2 Hz, 50 %), else bit n lit (T15).
            const uint32_t leds = s.panel.leds;
            on = (leds >> (16 + c_.id)) & 1u ? ed_.blinkOn() : ((leds >> c_.id) & 1u) != 0;
        }
        on = on && ed_.romError().isEmpty();
        const auto r = getLocalBounds().toFloat().reduced(4);
        if (on) {
            g.setColour(kLedOn.withAlpha(0.35f));
            g.fillRoundedRectangle(r.expanded(3), 4.0f);
        }
        g.setColour(on ? kLedOn : kLedOff);
        g.fillRoundedRectangle(r, 1.5f);
    }
};

class DisplayPart : public Part {
public:
    DisplayPart(PanelEditor& ed, const Control& c) : Part(ed, c) {
        setTooltip("Display (right-click: View menu)");
        setBounds(rect(c.r));
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.addSectionHeader("View");
        m.addItem("Show display codes", true, ed_.hexCodes(), [&ed = ed_] { ed.toggleHexCodes(); });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }
    void paint(juce::Graphics& g) override {
        box(g, getLocalBounds().toFloat(), kVfdBack, kEdge, 4.0f, 2.0f);
        // The 2 x 40 cells in kDisplayText, relative to the display window.
        const auto& t = ens::app::kDisplayText;
        const float x0 = static_cast<float>(t.x - c_.r.x), y0 = static_cast<float>(t.y - c_.r.y);
        const float cell = t.w / static_cast<float>(ens::ts::kPanelCols), rowH = t.h / 2.0f;
        const auto err = ed_.romError();
        const auto& p = ed_.snap().panel;
        g.setColour(kVfd);
        // DECISION A141: the glyph's ink (0.058-0.818 em above the baseline) centred in its row; one advance per cell.
        const auto vfd = vfdFont(cell);
        const float em = cell / 0.72f, baseDrop = rowH / 2.0f + 0.438f * em;
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < ens::ts::kPanelCols; ++col) {
                const juce::Rectangle<float> r(x0 + col * cell, y0 + row * rowH, cell, rowH);
                int code = ' ';
                bool hideChar = false, underline = false;
                if (err.isNotEmpty()) {
                    // No engine: the reason, in capitals (A141), two rows cut to 40 columns.
                    const auto line = displayMessageRow(err, row);
                    if (col < line.length()) code = static_cast<int>(line[col]) & 0x7F;
                } else {
                    code = static_cast<unsigned char>(p.text[row][col]);
                    // DECISION A113: a flashing cell blinks at 2 Hz, 50 % duty: its underline if it is
                    // underlined, else its character (the overlay shows the code throughout).
                    const bool flash = (p.blink[row] >> col) & 1u, off = flash && !ed_.blinkOn();
                    underline = ((p.underline[row] >> col) & 1u) != 0;
                    hideChar = off && !underline;
                    underline = underline && !off;
                }
                if (ed_.hexCodes()) {
                    g.setFont(monoFont(10.0f));
                    g.drawText(juce::String::toHexString(code).paddedLeft('0', 2).toUpperCase(), r,
                               juce::Justification::centred, false);
                } else if (!hideChar && code >= 0x20 && code <= 0x7E) {
                    // DECISION A113: 0x20-0x7E as ASCII; 0x00-0x1F and 0x7F blank (the overlay shows them).
                    // DECISION A141: drawn at its baseline, so a glyph is never dropped for its width.
                    juce::GlyphArrangement glyph;
                    glyph.addLineOfText(vfd, juce::String::charToString(static_cast<juce::juce_wchar>(code)),
                                        r.getX(), r.getY() + baseDrop);
                    glyph.draw(g);
                }
                if (underline) {
                    const auto bar = vfdCursorBar(cell, {r.getX(), r.getY() + baseDrop});
                    g.fillRoundedRectangle(bar, bar.getHeight() / 2.0f);
                }
            }
        }
    }
};

// DECISION A119: the disk drive drawn on the panel is the drive's control: a click opens its menu. The slot
// shows the drive's slot text (the file name, "(modified)"), read from the snapshot as the display is.
class DrivePart : public Part {
public:
    DrivePart(PanelEditor& ed, const Control& c) : Part(ed, c) {
        setTooltip("Disk drive: click for the disk menu, or drop a disk image on the panel");
        setBounds(rect(c.r));
    }
    void mouseDown(const juce::MouseEvent&) override { ed_.driveMenu(this); }
    void refresh() {
        if (ed_.slotText() != drawn_) repaint();
    }
    void paint(juce::Graphics& g) override {
        drawn_ = ed_.slotText();
        const auto r = getLocalBounds().toFloat();
        box(g, r, kBack, kButtonEdge, 4.0f, 2.0f);
        const auto slot = r.reduced(26.0f, 22.0f);
        box(g, slot, kGroove, kEdge, 2.0f);
        if (drawn_.isEmpty()) return;
        g.setColour(kText);
        g.setFont(labelFont(24.0f, false));
        g.drawFittedText(drawn_, slot.reduced(10.0f).toNearestInt(), juce::Justification::centred, 3, 0.7f);
    }

private:
    juce::AccessibilityActions actions() override {
        return juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                                      [this] { ed_.driveMenu(this); });
    }
    juce::String drawn_;
};

// The sliders and wheels are host parameters (A111), never Analog events: the processor writes the
// parameters into the machine every block. A vertical drag moves the position from where it was over
// the control's travel, up raising it (nothing is inverted on the TS, A110). The pitch wheel springs
// back to 0.5 when let go; the others stay put. A drag is one host gesture, so a DAW can record it.
class AnalogPart : public Part {
public:
    AnalogPart(PanelEditor& ed, const Control& c)
        : Part(ed, c), wheel_(c.kind == ControlKind::Wheel), param_(ed.param(paramId(c.id))) {
        setBounds(rect(c.r));
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (param_ == nullptr) return;
        dragging_ = true;
        param_->beginChangeGesture();
        from_ = param_->getValue();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragging_) return;
        const float up = (e.mouseDownPosition.y - e.position.y) / travel();
        param_->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, from_ + up));
    }
    void mouseUp(const juce::MouseEvent&) override { release(); }
    // Mouse-up, the app losing the foreground, or the editor closing.
    void release() {
        if (!dragging_) return;
        dragging_ = false;
        if (c_.id == static_cast<int>(ens::ts::Analog::PitchWheel)) param_->setValueNotifyingHost(0.5f);
        param_->endChangeGesture();
    }
    // The timer: repaint when the parameter moved (a drag, automation, the host's own controls).
    void refresh() {
        // != without -Wfloat-equal
        if (param_ != nullptr && (param_->getValue() < drawn_ || param_->getValue() > drawn_)) repaint();
    }

    void paint(juce::Graphics& g) override {
        drawn_ = param_ != nullptr ? param_->getValue() : 0.0f;
        const float v = drawn_;
        const auto r = getLocalBounds().toFloat();
        if (!wheel_) {
            box(g, r, kGroove, kTrackEdge, 3.0f);
            g.setColour(juce::Colours::black);
            g.fillRect(r.withSizeKeepingCentre(6.0f, r.getHeight() - 16.0f));
            const juce::Rectangle<float> cap(4.0f, (1 - v) * travel(), r.getWidth() - 8.0f, kCapH);
            box(g, cap, kCap, kCapEdge, 3.0f);
            g.setColour(kCapEdge.brighter(0.4f));
            g.fillRect(cap.withSizeKeepingCentre(cap.getWidth() - 8.0f, 2.0f));
            return;
        }
        juce::Path shape;
        shape.addRoundedRectangle(r, 10.0f);
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(shape);
        g.fillAll(kRidge);
        // The ridges roll with the wheel; the bar marks its position (top: pushed away from the player).
        g.setColour(kRidgeLine);
        for (float y = std::fmod((1 - v) * r.getHeight(), 7.0f) - 2; y < r.getHeight(); y += 7)
            g.fillRect(0.0f, y, r.getWidth(), 2.0f);
        g.setColour(kCapEdge);
        g.fillRect(0.0f, (1 - v) * (r.getHeight() - 6), r.getWidth(), 6.0f);
        g.setColour(kWheelEdge);
        g.strokePath(shape, juce::PathStrokeType(2.0f));
    }

private:
    static constexpr float kCapH = 26.0f;
    // A slider's cap travels the track less its own height; a wheel its full length.
    float travel() const { return static_cast<float>(getHeight()) - (wheel_ ? 0.0f : kCapH); }
    static const char* paramId(int analog) {
        switch (static_cast<ens::ts::Analog>(analog)) {
            case ens::ts::Analog::PitchWheel: return "pitchWheel";
            case ens::ts::Analog::ModWheel: return "modWheel";
            case ens::ts::Analog::Volume: return "volume";
            case ens::ts::Analog::CvPedal: return "cvPedal";
            case ens::ts::Analog::DataEntry:
            case ens::ts::Analog::Battery:
            case ens::ts::Analog::Reference:
            case ens::ts::Analog::Pressure:
            default: return "dataEntry";
        }
    }

    const bool wheel_;
    juce::RangedAudioParameter* param_;
    float from_ = 0, drawn_ = -1;
    bool dragging_ = false;
};

class KeyPart : public Part {
public:
    KeyPart(PanelEditor& ed, const Control& c, ens::ts::Model model) : Part(ed, c), model_(model) {
        const int n = c.id % 12;
        black_ = n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
        setBounds(rect(c.r));
    }
    // The board plays the key under the mouse, so a drag moves from key to key.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // DECISION A115: strike = 1 + round(126 x y / keyLength), y from the key's back edge: the front is loud.
    // DECISION A115: the TS-12's strike is 1-120 (its curve reaches 127 at 120, design "Velocity"): 119 x y / len.
    int strikeAt(float y) const {
        const float len = static_cast<float>(getHeight());
        const float span = model_ == ens::ts::Model::Ts12 ? 119.0f : 126.0f;
        return 1 + juce::roundToInt(span * juce::jlimit(0.0f, len, y) / len);
    }
    // A release carries the press's strike as its release value (the mouse has no release speed).
    void set(bool down, int strike = 0) {
        if (down) strike_ = static_cast<uint16_t>(strike);
        want_ = down;
        sync();
    }
    // As the buttons: sent until the engine takes it, so a full queue never leaves a stuck note.
    void sync() {
        if (want_ == down_ ||
            !ed_.post({ens::app::Event::Kind::Key, static_cast<uint8_t>(c_.id), want_, strike_}))
            return;
        down_ = want_;
        repaint();
    }
    void reset() {
        want_ = down_ = false;
        repaint();
    }
    // A key held on MIDI in (the snapshot's note mask, A107) is drawn pressed too.
    bool midiDown() const {
        const auto& n = ed_.snap().notes;
        return c_.id >= 0 && c_.id < 128 && ((n[static_cast<size_t>(c_.id) >> 6] >> (c_.id & 63)) & 1) != 0;
    }
    void refresh() {
        if (midiDown() != midi_) repaint();
    }

    void paint(juce::Graphics& g) override {
        midi_ = midiDown();
        const bool lit = down_ || midi_;
        const auto r = getLocalBounds().toFloat();
        juce::Path shape;
        const float rad = black_ ? 3.0f : 4.0f;
        shape.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad, false, false, true,
                                  true);
        g.setColour(black_ ? (lit ? kBlackShade : kBlack) : (lit ? kWhite.darker(0.15f) : kWhite));
        g.fillPath(shape);
        if (black_) {
            g.setColour(kBlackShade);
            g.fillRect(r.reduced(1).withTop(r.getBottom() - 8));
        }
        g.setColour(black_ ? juce::Colours::black : kWhiteEdge);
        g.strokePath(shape, juce::PathStrokeType(1.0f));
    }

private:
    ens::ts::Model model_;
    bool black_ = false, want_ = false, down_ = false, midi_ = false;
    uint16_t strike_ = 1;
};

// The panel at its design size; the editor scales it with a transform.
class Board : public juce::Component {
public:
    Board(PanelEditor& ed, ens::ts::Model model) {
        setSize(ens::app::kDesignW, ens::app::kDesignH);
        // Order: the table's (the display before its soft buttons; the white keys, then the blacks over them).
        for (const Control& c : ens::app::panelLayout(model)) {
            std::unique_ptr<Part> p;
            switch (c.kind) {
                case ControlKind::Button:
                case ControlKind::Patch: {
                    using ens::ts::Button;
                    if (c.kind == ControlKind::Button &&
                        (c.id == static_cast<int>(Button::Up) || c.id == static_cast<int>(Button::Down)))
                        arrows_.push_back(&c);
                    auto b = std::make_unique<ButtonPart>(ed, c);
                    buttons_.push_back(b.get());
                    p = std::move(b);
                    break;
                }
                case ControlKind::Led: {
                    auto l = std::make_unique<LedPart>(ed, c);
                    leds_.push_back(l.get());
                    p = std::move(l);
                    break;
                }
                case ControlKind::Display:
                    p = std::make_unique<DisplayPart>(ed, c);
                    display_ = p.get();
                    break;
                case ControlKind::Slider:
                case ControlKind::Wheel: {
                    auto a = std::make_unique<AnalogPart>(ed, c);
                    analogs_.push_back(a.get());
                    p = std::move(a);
                    break;
                }
                case ControlKind::Key: {
                    auto k = std::make_unique<KeyPart>(ed, c, model);
                    keys_.push_back(k.get());
                    p = std::move(k);
                    break;
                }
                case ControlKind::Decor: {
                    auto d = std::make_unique<DrivePart>(ed, c);
                    drive_ = d.get();
                    p = std::move(d);
                    break;
                }
                case ControlKind::Label: painted_.push_back(&c); continue;
            }
            if (c.r.y >= ens::app::kTopH) {  // A139: the strip's parts live in its layer, in its coordinates
                p->setTopLeftPosition(p->getX(), p->getY() - ens::app::kTopH);
                layer_.addAndMakeVisible(*p);
            } else {
                addAndMakeVisible(*p);
            }
            parts_.push_back(std::move(p));
        }
        // DECISION A139: the strip is a layer inside a window (clip_) that starts at kTopH: moving the layer up slides
        // the strip under the top panel, which clip_'s top edge cuts it at.
        clip_.setBounds(0, ens::app::kTopH, ens::app::kDesignW, ens::app::kDesignH - ens::app::kTopH);
        layer_.setBounds(clip_.getLocalBounds());
        clip_.setInterceptsMouseClicks(false, true);
        clip_.addAndMakeVisible(layer_);
        addAndMakeVisible(clip_);
    }

    // DECISION A139: the strip (everything at y >= kTopH) moved up by dy design units, under the top panel: 0 in place,
    // kDesignH - kTopH all the way under, where it is hidden. Only a strip in place takes the mouse.
    void setStripOffset(int dy) {
        layer_.setTopLeftPosition(0, -dy);
        clip_.setVisible(dy < ens::app::kDesignH - ens::app::kTopH);
        clip_.setInterceptsMouseClicks(false, dy == 0);
    }
    // Before the strip goes: its key, its patch buttons (latched too) and its wheels released; the pitch wheel springs
    // back, the mod wheel keeps its value (AnalogPart::release).
    void releaseStrip() {
        keyUp();
        for (auto* b : buttons_)
            if (b->getParentComponent() == &layer_) b->release(true);
        for (auto* a : analogs_)
            if (a->getParentComponent() == &layer_) a->release();
    }

    void release(bool latchedToo) {
        for (auto* b : buttons_) b->release(latchedToo);
        keyUp();
        for (auto* a : analogs_) a->release();
    }
    // The timer: whatever the queue could not take yet (A102).
    void sync() {
        for (auto* b : buttons_) b->sync();
        for (auto* k : keys_) k->sync();
    }
    // After the snapshot is taken: what the parameters and MIDI in changed.
    void refresh() {
        for (auto* k : keys_) k->refresh();
        for (auto* a : analogs_) a->refresh();
        if (drive_ != nullptr) drive_->refresh();
    }
    // A new engine: nothing held, nothing latched; nothing is posted.
    void reset() {
        keyDrag_ = false;
        held_ = nullptr;
        for (auto* b : buttons_) b->reset();
        for (auto* k : keys_) k->reset();
    }

    // The keyboard's mouse. Mouse-down plays the key under it; a drag releases the key it leaves and
    // plays the key it enters, each with the strike where it was entered (A115); mouse-up releases,
    // wherever the mouse is.
    void keyDown(const juce::MouseEvent& e) {
        keyDrag_ = true;
        keyMove(e);
    }
    void keyMove(const juce::MouseEvent& e) {
        if (!keyDrag_) return;
        const auto p = e.getEventRelativeTo(&layer_).position;  // the keys' parent (A139)
        KeyPart* k = nullptr;
        for (auto it = keys_.rbegin(); it != keys_.rend() && k == nullptr; ++it)  // blacks first: on top
            if ((*it)->getBounds().toFloat().contains(p)) k = *it;
        if (k == held_) return;
        if (held_ != nullptr) held_->set(false);
        held_ = k;
        if (k != nullptr) k->set(true, k->strikeAt(p.y - static_cast<float>(k->getY())));
    }
    void keyUp() {
        keyDrag_ = false;
        if (held_ != nullptr) held_->set(false);
        held_ = nullptr;
    }
    void repaintPanel() {
        display_->repaint();
        for (auto* l : leds_) l->repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(kPanel);  // the top panel; the strip's layer paints the rest (A139)
        for (const Control* c : painted_)
            if (c->r.y < ens::app::kTopH) paintLabel(g, c);

        // The outline arrows printed above Up and below Down (the photo).
        g.setColour(kText);
        for (const Control* c : arrows_) {
            const bool up = c->id == static_cast<int>(ens::ts::Button::Up);
            const float x = c->r.x + c->r.w / 2.0f, y = up ? c->r.y - 9.0f : static_cast<float>(c->r.y + c->r.h + 9);
            const float tip = up ? -4.0f : 4.0f;
            juce::Path t;
            t.addTriangle(x - 7, y - tip, x + 7, y - tip, x, y + tip);
            g.strokePath(t, juce::PathStrokeType(1.5f));
        }
    }
    // The strip's body as the photo, in board coordinates: the panel down to the brushed strip's edge, the key bed, the
    // wheel box and the strip's labels (A139: drawn by the layer, which moves with it).
    void paintStrip(juce::Graphics& g) {
        g.fillAll(kPanel);
        g.setColour(kEdge);
        g.fillRect(60, 395, 2470, 2);
        g.setColour(kBack);
        g.fillRect(0, 540, ens::app::kDesignW, ens::app::kDesignH - 540);
        box(g, {62.0f, 552.0f, 266.0f, 352.0f}, kPanel, kEdge, 6.0f);
        g.setColour(kGroove);
        g.fillRect(330, 548, 2150, 362);
        for (const Control* c : painted_)
            if (c->r.y >= ens::app::kTopH) paintLabel(g, c);
    }

private:
    void paintLabel(juce::Graphics& g, const Control* c) {
        {
            const auto r = rect(c->r);
            if (c->ink == Ink::Decal) {
                // DECISION A122: the logo and the Ensoniq badge are drawn from their SVGs, kept to their
                // shapes; the line under the logo is text, tall and narrow, stretched or squeezed to the
                // photo's width. DECISION A140: the model's own logo, by the decal's label.
                juce::Drawable* art = std::strcmp(c->label, "TS-10") == 0     ? ts10_.get()
                                      : std::strcmp(c->label, "TS-12") == 0   ? ts12_.get()
                                      : std::strcmp(c->label, "ensoniq") == 0 ? ensoniq_.get()
                                                                              : nullptr;
                // DECISION A140: a logo is fitted by its viewBox (the SVG's content area), not its ink, so the
                // letters the two logos share are drawn at one scale on both models; the badge by its ink, as
                // its rect was measured.
                auto* svg = art == ensoniq_.get() ? nullptr : dynamic_cast<juce::DrawableComposite*>(art);
                if (svg != nullptr) {
                    svg->draw(g, 1.0f, juce::RectanglePlacement(juce::RectanglePlacement::centred)
                                            .getTransformToFit(svg->getContentArea(), r.toFloat()));
                    return;
                }
                if (art != nullptr) {
                    art->drawWithin(g, r.toFloat(), juce::RectanglePlacement::centred, 1.0f);
                    return;
                }
                auto font = labelFont(c->r.h * 1.3f, false).withExtraKerningFactor(0.08f);
                const float w = juce::GlyphArrangement::getStringWidth(font, c->label);
                if (w > 0) font = font.withHorizontalScale(static_cast<float>(c->r.w) / w);
                g.setColour(kDecal);
                g.setFont(font);
                g.drawText(c->label, r, juce::Justification::centred, false);
                return;
            }
            // A box 20 or more high holds two lines (the photo's button labels), even when the text has
            // one, which is centred. Each line is squeezed, never wrapped.
            // DECISION A123: the font is larger than the line (Segoe UI's letters fill about 70 % of its
            // height), so the letters, not the font's leading, fill it: kLabelGrow for a button's two-line
            // label, kHeadGrow for a one-line label. A two-line label's lines are kLineGap lines apart,
            // the second on the box's centre line as before, the first moved up, away from the button.
            // A line wider than its box, or than kLabelRoom of a two-line box (neighbouring 44-wide boxes are
            // 40 apart, so this keeps a gap between them), is squeezed sideways down to half width, never
            // shrunk.
            const auto lines = juce::StringArray::fromLines(c->label);
            const bool two = c->r.h >= 20;
            const int lineH = two ? c->r.h / 2 : c->r.h;
            const int last = lines.size() - 1;
            const float lastY = r.getY() + c->r.h / 2.0f + last * lineH / 2.0f;  // the last line's centre
            const auto font = labelFont(lineH * (two ? kLabelGrow : kHeadGrow), c->ink == Ink::Bright);
            const float room = r.getWidth() * (two ? kLabelRoom : 1.0f), fh = font.getHeight();
            g.setColour(c->ink == Ink::Bright ? kBright : c->ink == Ink::Dim ? kDim : kText);
            for (int i = 0; i < lines.size(); ++i) {
                const float w = juce::GlyphArrangement::getStringWidth(font, lines[i]);
                g.setFont(w > room ? font.withHorizontalScale(std::max(0.5f, room / w)) : font);
                const float cy = lastY - (last - i) * lineH * kLineGap;
                const auto x = static_cast<float>(r.getX() - r.getWidth());
                g.drawText(lines[i], juce::Rectangle<float>(x, cy - fh / 2, r.getWidth() * 3.0f, fh),
                           juce::Justification::centred, false);
            }
        }
    }

    // A139: the strip's layer: paints the strip's body in its own coordinates (kTopH below the board's).
    struct Layer : juce::Component {
        explicit Layer(Board& b) : board(b) {}
        void paint(juce::Graphics& g) override {
            g.setOrigin(0, -ens::app::kTopH);
            board.paintStrip(g);
        }
        Board& board;
    };

    std::vector<std::unique_ptr<Part>> parts_;
    std::vector<ButtonPart*> buttons_;
    std::vector<LedPart*> leds_;
    std::vector<KeyPart*> keys_;  // the table's order: whites, then blacks
    std::vector<AnalogPart*> analogs_;
    KeyPart* held_ = nullptr;  // the key the mouse plays
    bool keyDrag_ = false;
    DrivePart* drive_ = nullptr;
    std::unique_ptr<juce::Drawable> ts10_ = art(TsArt::ts10_logo_svg, TsArt::ts10_logo_svgSize);
    std::unique_ptr<juce::Drawable> ts12_ = art(TsArt::ts12_logo_svg, TsArt::ts12_logo_svgSize);
    std::unique_ptr<juce::Drawable> ensoniq_ = art(TsArt::ensoniq_logo_svg, TsArt::ensoniq_logo_svgSize, kBadge);
    std::vector<const Control*> painted_;  // labels: the top's drawn by the board, the strip's by its layer
    juce::Component clip_;  // A139: the strip's window, kTopH down; layer_ moves up inside it
    Layer layer_{*this};
    std::vector<const Control*> arrows_;   // Up and Down
    juce::Component* display_ = nullptr;
};

// The settings page in its own window, as tall as the page within the screen; the rest scrolls.
class SettingsWindow : public juce::DocumentWindow, private juce::ComponentListener {
public:
    explicit SettingsWindow(std::unique_ptr<SettingsPage> page)
        : DocumentWindow("TS Reborn Settings", kPanel, closeButton) {
        setUsingNativeTitleBar(true);
        page->addComponentListener(this);
        view_.setViewedComponent(page.release(), true);
        view_.setScrollBarsShown(true, false);
        setContentNonOwned(&view_, true);  // the window follows the view's size
        fit();
    }
    ~SettingsWindow() override {
        view_.getViewedComponent()->removeComponentListener(this);
        clearContentComponent();
    }
    void closeButtonPressed() override { setVisible(false); }

private:
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override {
        if (wasResized) fit();
    }
    void fit() {
        const auto* page = view_.getViewedComponent();
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        // The screen's height less the title bar.
        const int room =
            display != nullptr ? static_cast<int>(display->userBounds.getHeight()) - 48 : page->getHeight();
        const int h = std::min(page->getHeight(), room);
        view_.setSize(page->getWidth() + (h < page->getHeight() ? view_.getScrollBarThickness() : 0), h);
    }

    juce::Viewport view_;
};

Board& board(juce::Component& c) { return static_cast<Board&>(c); }

// A139: a key's parent is the strip's layer; its board is further up.
void KeyPart::mouseDown(const juce::MouseEvent& e) { findParentComponentOfClass<Board>()->keyDown(e); }
void KeyPart::mouseDrag(const juce::MouseEvent& e) { findParentComponentOfClass<Board>()->keyMove(e); }
void KeyPart::mouseUp(const juce::MouseEvent&) { findParentComponentOfClass<Board>()->keyUp(); }

}  // namespace

// DECISION A141: Ensoniq VFD Regular (tymcode/ensoniq-vfd-font, SIL OFL 1.1), compiled in unchanged. JUCE sizes a
// font by its line height, so the height for one advance per cell is measured once at 100.
juce::Font vfdFont(float cellWidth) {
    static const juce::Typeface::Ptr face =
        juce::Typeface::createSystemTypefaceFor(TsArt::EnsoniqVFD_otf, static_cast<size_t>(TsArt::EnsoniqVFD_otfSize));
    if (face == nullptr) return monoFont(cellWidth / 0.6f);
    static const float advanceAt100 =
        juce::GlyphArrangement::getStringWidth(juce::Font(juce::FontOptions(face)).withHeight(100.0f), "A");
    return juce::Font(juce::FontOptions(face)).withHeight(100.0f * cellWidth / advanceAt100);
}

// DECISION A141: the cursor bar where the font's Cursor weight draws it under the cell: x 0.053-0.509 em from the
// cell's left, 0.086-0.164 em below the baseline (one advance, 0.72 em, is the cell's width).
juce::Rectangle<float> vfdCursorBar(float cellWidth, juce::Point<float> baselineLeft) {
    const float em = cellWidth / 0.72f;
    return {baselineLeft.x + 0.053f * em, baselineLeft.y + 0.086f * em, 0.456f * em, 0.078f * em};
}

// DECISION A141: the display shows capitals only, as the instrument's; the settings page keeps a file's exact name.
juce::String displayMessageRow(const juce::String& message, int row) {
    return juce::StringArray::fromLines(message)[row].toUpperCase();
}

PanelEditor::PanelEditor(TsProcessor& p) : AudioProcessorEditor(p), proc_(p), engine_(p.engine()) {
    if (engine_ != nullptr) snap_ = engine_->snapshot();
    built_ = p.model();
    board_ = std::make_unique<Board>(*this, built_);
    addAndMakeVisible(*board_);
    // DECISION A112: 1546 x 552 by default, resizable from 1030 x 368 up, aspect fixed at kDesignW:kDesignH.
    // DECISION A139: 1546 x 258 with the keyboard strip out (aspect kDesignW:kTopH), as the setting says.
    setResizable(true, true);
    shown_ = full_ = AppSettings().keyboardShown();
    offset_ = from_ = to_ = shown_ ? 0 : ens::app::kDesignH - ens::app::kTopH;
    board(*board_).setStripOffset(static_cast<int>(offset_));
    setSize(1546, 552);
    fitSize(full_);
    startTimerHz(30);
}

PanelEditor::~PanelEditor() { board(*board_).release(true); }

// DECISION A139: the panel shown in full (kDesignH) or the top only (kTopH) fits the window, as A112's letterbox did:
// a window wider than that shape (a host ignoring the aspect) is fitted by its height, not cut off (review fix).
void PanelEditor::resized() {
    const auto h = static_cast<float>(ens::app::kDesignH - offset_);  // the panel down to the strip's bottom
    board_->setTransform(juce::AffineTransform::scale(
        std::min(getWidth() / static_cast<float>(ens::app::kDesignW), getHeight() / h)));
}

// DECISION A139 (approach A): hiding releases the strip, slides it under the top panel, then shrinks the window;
// showing grows the window, then slides it down. The Standalone (File mode: its own window, no host) instead resizes
// with the slide, the window's bottom following the strip (the user's choice); a plugin keeps one host resize each
// way. A click mid-slide reverses from where the strip is. The state is stored at once.
void PanelEditor::toggleKeyboard() {
    shown_ = !shown_;
    AppSettings().setKeyboardShown(shown_);
    if (!shown_) board(*board_).releaseStrip();
    if (follows()) {
        // Any height from the top panel's to the whole panel's at this width, while the slide sets it.
        full_ = true;
        setLimits(0, ens::app::kTopH, ens::app::kDesignH);
    } else if (shown_ && !full_) {
        fitSize(true);
    }
    from_ = offset_;
    to_ = shown_ ? 0 : ens::app::kDesignH - ens::app::kTopH;
    startMs_ = juce::Time::getMillisecondCounterHiRes();
    slide_.startTimerHz(60);
}

// About 250 ms for the whole way, ease-out; a shorter way takes its share.
void PanelEditor::stepSlide() {
    constexpr double kSlideMs = 250, kOut = ens::app::kDesignH - ens::app::kTopH;
    const double ms = std::max(1.0, kSlideMs * std::abs(to_ - from_) / kOut);
    const double t = std::min(1.0, (juce::Time::getMillisecondCounterHiRes() - startMs_) / ms);
    offset_ = from_ + (to_ - from_) * (1 - (1 - t) * (1 - t));
    board(*board_).setStripOffset(static_cast<int>(std::lround(offset_)));
    if (follows())  // the window's bottom at the strip's
        setSize(getWidth(), static_cast<int>(std::lround(getWidth() * (ens::app::kDesignH - offset_) / ens::app::kDesignW)));
    if (t < 1) return;
    slide_.stopTimer();
    if (follows() || !shown_) fitSize(shown_);
}

bool PanelEditor::follows() const { return proc_.memoryMode() == MemoryMode::File; }

// The limits and aspect for the full panel or the top only; the width stays, the height follows. The constrainer is
// set before the size and set directly: the Standalone's window, and Windows for every native resize
// (juce_Windowing_windows.cpp, handlePositionChanging), check the new size against the editor's constrainer, so limits
// set after it kept the window at the old minimum (2424 x 367 around a 1546 x 234 panel: the user's check); and
// setResizeLimits would constrain the old bounds first, a second host resize (review fix).
void PanelEditor::fitSize(bool full) {
    full_ = full;
    const int h = full ? ens::app::kDesignH : ens::app::kTopH, w = getWidth();
    setLimits(static_cast<double>(ens::app::kDesignW) / h, h, h);
    setSize(w, static_cast<int>(std::lround(static_cast<double>(w) * h / ens::app::kDesignW)));
}

// Widths 1030 to 2 x kDesignW, heights for design heights lowH to highH at those widths, and the aspect (0: free).
void PanelEditor::setLimits(double aspect, int lowH, int highH) {
    auto* c = getConstrainer();
    c->setSizeLimits(1030, 1030 * lowH / ens::app::kDesignW, 2 * ens::app::kDesignW, 2 * highH);
    c->setFixedAspectRatio(aspect);
}

juce::RangedAudioParameter* PanelEditor::param(const char* id) { return proc_.params().getParameter(id); }

bool PanelEditor::post(const ens::app::Event& e) {
    const bool ok = sameEngine() && engine_ != nullptr && engine_->post(e);
    if (ok) lastPosted_ = e;
    return ok;
}

// Erase and a ROM folder change replace the engine (the new one is made before the old one is freed,
// so the pointer differs). The new machine starts with nothing held: the parts forget the old engine's
// state, and nothing is sent to the new one (the parameters carry on).
bool PanelEditor::sameEngine() {
    auto* e = proc_.engine();
    if (e == engine_) return true;
    engine_ = e;
    board(*board_).reset();
    return false;
}

// Before the machine runs the display gives the reason. A machine with no audio device open
// (prepareToPlay not yet run) does not run.
juce::String PanelEditor::romError() const {
    if (!proc_.engine()) return proc_.romError();
    return proc_.prepared() ? juce::String() : juce::String("No audio device");
}

juce::String PanelEditor::slotText() const { return proc_.drive().slotText(); }

void PanelEditor::driveMenu(juce::Component* target) { proc_.drive().showMenu(target); }

// DECISION A119: a file dropped anywhere on the panel goes in the drive (asking first, A120).
// DECISION A128: an .iso or .cue (any case) goes in the CD drive instead; DECISION A136: a .syx into MIDI in.
void PanelEditor::filesDropped(const juce::StringArray& files, int, int) {
    if (files.size() != 1) return;
    const juce::File f(files[0]);
    if (f.hasFileExtension("iso;cue")) {
        proc_.cdDrive().insertAsked(f);
    } else if (f.hasFileExtension("syx")) {  // DECISION A136: a SysEx file goes into MIDI in
        juce::String err;
        if (!proc_.sendSysex(f, err))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "SysEx", err);
    } else
        proc_.drive().openAsked(f);
}

void PanelEditor::toggleHexCodes() {
    hexCodes_ = !hexCodes_;
    board(*board_).repaintPanel();
}

void PanelEditor::openSettings() {
    if (settings_ == nullptr) {
        auto page = std::make_unique<SettingsPage>(proc_);
        page->onChange = [this] { board_->repaint(); };
        settings_ = std::make_unique<SettingsWindow>(std::move(page));
        settings_->centreAroundComponent(this, settings_->getWidth(), settings_->getHeight());
    }
    settings_->setVisible(true);
    settings_->toFront(true);
}

void PanelEditor::timerCallback() {
    sameEngine();
    // DECISION A137: a model switch (the settings page, a loaded state) replaces the board, keeping the editor's
    // size. sameEngine() has reset the old parts first, so nothing is posted for them; the old board and every
    // pointer into it (the parts, the arrows) go with it.
    if (proc_.model() != built_) {
        built_ = proc_.model();
        board_ = std::make_unique<Board>(*this, built_);
        board(*board_).setStripOffset(static_cast<int>(std::lround(offset_)));  // A139: the strip where it was
        addAndMakeVisible(*board_);
        resized();
    }
    auto& b = board(*board_);
    b.sync();  // presses and releases the full queue turned away (A102)
    const auto old = snap_;
    if (engine_ != nullptr) snap_ = engine_->snapshot();
    b.refresh();
    // DECISION A113: blink at 2 Hz, 50 % duty, from the message-thread clock.
    const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    const auto &o = old.panel, &n = snap_.panel;
    const bool blinking = (n.blink[0] | n.blink[1]) != 0 || (n.leds >> 16) != 0;
    const bool changed = std::memcmp(o.text, n.text, sizeof n.text) != 0 ||
                         std::memcmp(o.blink, n.blink, sizeof n.blink) != 0 ||
                         std::memcmp(o.underline, n.underline, sizeof n.underline) != 0 || o.leds != n.leds ||
                         old.seqLed != snap_.seqLed || (blinking && blink != blinkOn_);
    blinkOn_ = blink;

    // DECISION A114: buttons held when the app loses the foreground are released (Alt-Tab while held).
    const bool fg = juce::Process::isForegroundProcess();
    if (foreground_ && !fg) b.release(false);
    foreground_ = fg;

    // Once a second too, for romError (the engine made or the audio device opened).
    if (++ticks_ % 30 == 0 || changed) b.repaintPanel();
}
