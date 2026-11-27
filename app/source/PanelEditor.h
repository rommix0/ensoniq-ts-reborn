#pragma once
// The front-panel editor (from KS-32 Reborn's): the TS-10 panel drawn from the JUCE-free layout table
// (src/app/panel_layout.h), scaled uniformly to the window (A112). The display and LEDs follow the
// engine's snapshot (A113); the buttons post press and release, with the chord latch (A114); the sliders
// and wheels are the host parameters (A111), and the keys play with a strike (A115). The gear opens the
// settings page; the disk drive opens the drive's menu, and a file dropped on the panel goes in the drive (A119).
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "app/engine.h"

class TsProcessor;

class PanelEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit PanelEditor(TsProcessor& p);
    ~PanelEditor() override;
    void resized() override;

    // Used by the parts.
    // False when the queue is full (A102), there is no engine, or the engine was just replaced (the
    // parts are then reset and nothing reaches the new engine). The parts retry from the timer.
    bool post(const ens::app::Event& e);
    juce::RangedAudioParameter* param(const char* id);  // a host parameter (A111)
    void openSettings();
    const ens::app::Snapshot& snap() const { return snap_; }
    const ens::app::Event& lastPosted() const { return lastPosted_; }  // the last event the engine took (checks)
    bool blinkOn() const { return blinkOn_; }
    bool hexCodes() const { return hexCodes_; }
    void toggleHexCodes();
    juce::String romError() const;  // empty while the engine runs
    juce::String slotText() const;  // the disk drive's (A119)
    void driveMenu(juce::Component* target);
    // DECISION A139: the keyboard strip. toggleKeyboard starts the slide toward the other state (or reverses one under
    // way) and stores it; keyboardShown is that target state; sliding is true while the strip moves.
    void toggleKeyboard();
    bool keyboardShown() const { return shown_; }
    bool sliding() const { return slide_.isTimerRunning(); }

    bool isInterestedInFileDrag(const juce::StringArray& files) override { return files.size() == 1; }
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    bool sameEngine();  // false, after resetting the parts, when the engine was replaced
    void stepSlide();        // A139: the slide timer
    void fitSize(bool full);  // A139: the limits, aspect and height for the full panel or the top only
    void setLimits(double aspect, int lowH, int highH);  // A139: the constrainer, without a resize
    bool follows() const;     // A139: the window resizes with the slide (the Standalone)

    TsProcessor& proc_;
    ens::app::Engine* engine_ = nullptr;  // the engine the parts' state belongs to
    std::unique_ptr<juce::Component> board_;  // kDesignW x kDesignH design units, scaled by a transform
    juce::TooltipWindow tooltips_{this};
    std::unique_ptr<juce::DocumentWindow> settings_;
    ens::ts::Model built_ = ens::ts::Model::Ts10;  // the model the board was built for (A137)
    ens::app::Event lastPosted_{ens::app::Event::Kind::Button, 0, false, 0};
    ens::app::Snapshot snap_{};
    bool blinkOn_ = true, hexCodes_ = false, foreground_ = true;
    int ticks_ = 0;
    // A139: the strip's offset in design units (0 in place, kDesignH - kTopH out), the slide's start and target,
    // when it started, and whether the window has the full panel's size.
    bool shown_ = true, full_ = true;
    double offset_ = 0, from_ = 0, to_ = 0, startMs_ = 0;
    juce::TimedCallback slide_{[this] { stepSlide(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelEditor)
};
