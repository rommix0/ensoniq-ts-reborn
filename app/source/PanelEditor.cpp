#include "PanelEditor.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "Processor.h"
#include "SettingsPage.h"
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
    kVfd{0xff7fe8d6}, kVfdBack{0xff050807}, kLedOn{0xffff4a3d}, kLedOff{0xff4a2522}, kDecal{0xff8fb3ad};
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
        : Part(ed, c), gear_(c.id == ens::app::kSettingsId),
          kind_(c.kind == ControlKind::Patch ? ens::app::Event::Kind::PatchSelect : ens::app::Event::Kind::Button) {
        setWantsKeyboardFocus(true);
        setBounds(rect(c.r));
    }

    // DECISION A114: a right-click or Shift-click latches the button down until it is clicked again; a
    // plain press is sent on mouse-down and its release on mouse-up, wherever the mouse is (as KS-32 A9).
    void mouseDown(const juce::MouseEvent& e) override {
        if (gear_) return;
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
        if (gear_) {
            if (getLocalBounds().contains(e.getPosition())) ed_.openSettings();
        } else if (byMouse_) {
            byMouse_ = false;
            if (!latched_) set(false);
        }
    }
    // Space presses the focused button while it is held.
    bool keyPressed(const juce::KeyPress& k) override {
        if (k != juce::KeyPress::spaceKey) return false;
        if (gear_) {
            ed_.openSettings();
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
        if (gear_ || want_ == down_ || !ed_.post({kind_, static_cast<uint8_t>(c_.id), want_, 0})) return;
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
        if (hasKeyboardFocus(false)) {
            g.setColour(kFocus);
            g.drawRoundedRectangle(r.reduced(1.0f), 3.0f, 2.0f);
        }
    }

private:
    juce::AccessibilityActions actions() override {
        return juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] {
            if (gear_) {
                ed_.openSettings();
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

    const bool gear_;
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
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < ens::ts::kPanelCols; ++col) {
                const juce::Rectangle<float> r(x0 + col * cell, y0 + row * rowH, cell, rowH);
                int code = ' ';
                bool hideChar = false, underline = false;
                if (err.isNotEmpty()) {
                    // No engine: the reason, two rows cut to 40 columns.
                    const auto line = juce::StringArray::fromLines(err)[row];
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
                    g.setFont(monoFont(rowH * 0.8f));
                    g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(code)), r,
                               juce::Justification::centred, false);
                }
                if (underline) g.fillRect(r.getX() + 1.5f, r.getBottom() - 2.5f, cell - 3.0f, 2.0f);
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
        if (param_ != nullptr && (param_->getValue() < drawn_ || param_->getValue() > drawn_)) repaint();  // != without -Wfloat-equal
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
    KeyPart(PanelEditor& ed, const Control& c) : Part(ed, c) {
        const int n = c.id % 12;
        black_ = n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
        setBounds(rect(c.r));
    }
    // The board plays the key under the mouse, so a drag moves from key to key.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // DECISION A115: strike = 1 + round(126 x y / keyLength), y from the key's back edge: the front is loud.
    int strikeAt(float y) const {
        const float len = static_cast<float>(getHeight());
        return 1 + juce::roundToInt(126.0f * juce::jlimit(0.0f, len, y) / len);
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
    bool black_ = false, want_ = false, down_ = false, midi_ = false;
    uint16_t strike_ = 1;
};

// The panel at its design size; the editor scales it with a transform.
class Board : public juce::Component {
public:
    explicit Board(PanelEditor& ed) {
        setSize(ens::app::kDesignW, ens::app::kDesignH);
        // Order: the table's (the display before its soft buttons; the white keys, then the blacks over them).
        for (const Control& c : ens::app::panelLayout()) {
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
                    auto k = std::make_unique<KeyPart>(ed, c);
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
            addAndMakeVisible(*p);
            parts_.push_back(std::move(p));
        }
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
        const auto p = e.getEventRelativeTo(this).position;
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
        // The body as the photo: the panel, the brushed strip's edge, the key bed and the wheel box.
        g.fillAll(kPanel);
        g.setColour(kEdge);
        g.fillRect(60, 395, 2470, 2);
        g.setColour(kBack);
        g.fillRect(0, 540, ens::app::kDesignW, ens::app::kDesignH - 540);
        box(g, {62.0f, 552.0f, 266.0f, 352.0f}, kPanel, kEdge, 6.0f);
        g.setColour(kGroove);
        g.fillRect(330, 548, 2150, 362);

        for (const Control* c : painted_) {
            const auto r = rect(c->r);
            if (c->ink == Ink::Decal) {
                // The model decal: "TS-10" and its line, no maker's name or logo.
                g.setColour(kDecal);
                g.setFont(labelFont(static_cast<float>(c->r.h), c->r.h > 40));
                g.drawFittedText(c->label, r, juce::Justification::centred, 1, 0.3f);
                continue;
            }
            // A box 20 or more high holds two lines (the photo's button labels), even when the text has
            // one, which is centred. Each line is squeezed, never wrapped.
            const auto lines = juce::StringArray::fromLines(c->label);
            const int lineH = c->r.h >= 20 ? c->r.h / 2 : c->r.h;
            const int y0 = r.getY() + (c->r.h - lines.size() * lineH) / 2;
            g.setColour(c->ink == Ink::Bright ? kBright : c->ink == Ink::Dim ? kDim : kText);
            g.setFont(labelFont(static_cast<float>(lineH), c->ink == Ink::Bright));
            for (int i = 0; i < lines.size(); ++i)
                g.drawFittedText(lines[i], r.getX(), y0 + i * lineH, r.getWidth(), lineH, juce::Justification::centred,
                                 1, 0.5f);
        }

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

private:
    std::vector<std::unique_ptr<Part>> parts_;
    std::vector<ButtonPart*> buttons_;
    std::vector<LedPart*> leds_;
    std::vector<KeyPart*> keys_;  // the table's order: whites, then blacks
    std::vector<AnalogPart*> analogs_;
    KeyPart* held_ = nullptr;  // the key the mouse plays
    bool keyDrag_ = false;
    DrivePart* drive_ = nullptr;
    std::vector<const Control*> painted_;  // labels, drawn by the board
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

void KeyPart::mouseDown(const juce::MouseEvent& e) { board(*getParentComponent()).keyDown(e); }
void KeyPart::mouseDrag(const juce::MouseEvent& e) { board(*getParentComponent()).keyMove(e); }
void KeyPart::mouseUp(const juce::MouseEvent&) { board(*getParentComponent()).keyUp(); }

}  // namespace

PanelEditor::PanelEditor(TsProcessor& p) : AudioProcessorEditor(p), proc_(p), engine_(p.engine()) {
    if (engine_ != nullptr) snap_ = engine_->snapshot();
    board_ = std::make_unique<Board>(*this);
    addAndMakeVisible(*board_);
    // DECISION A112: 1546 x 552 by default, resizable from 1030 x 368 up, aspect fixed at kDesignW:kDesignH.
    setResizable(true, true);
    setResizeLimits(1030, 368, 2 * ens::app::kDesignW, 2 * ens::app::kDesignH);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(ens::app::kDesignW) / ens::app::kDesignH);
    setSize(1546, 552);
    startTimerHz(30);
}

PanelEditor::~PanelEditor() { board(*board_).release(true); }

void PanelEditor::resized() {
    const float s = std::min(getWidth() / static_cast<float>(ens::app::kDesignW),
                             getHeight() / static_cast<float>(ens::app::kDesignH));
    board_->setTransform(juce::AffineTransform::scale(s));
}

juce::RangedAudioParameter* PanelEditor::param(const char* id) { return proc_.params().getParameter(id); }

bool PanelEditor::post(const ens::app::Event& e) { return sameEngine() && engine_ != nullptr && engine_->post(e); }

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
void PanelEditor::filesDropped(const juce::StringArray& files, int, int) {
    if (files.size() == 1) proc_.drive().openAsked(juce::File(files[0]));
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
    auto& b = board(*board_);
    sameEngine();
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
