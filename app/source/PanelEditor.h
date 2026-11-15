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
    bool blinkOn() const { return blinkOn_; }
    bool hexCodes() const { return hexCodes_; }
    void toggleHexCodes();
    juce::String romError() const;  // empty while the engine runs
    juce::String slotText() const;  // the disk drive's (A119)
    void driveMenu(juce::Component* target);

    bool isInterestedInFileDrag(const juce::StringArray& files) override { return files.size() == 1; }
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    bool sameEngine();  // false, after resetting the parts, when the engine was replaced

    TsProcessor& proc_;
    ens::app::Engine* engine_ = nullptr;  // the engine the parts' state belongs to
    std::unique_ptr<juce::Component> board_;  // kDesignW x kDesignH design units, scaled by a transform
    juce::TooltipWindow tooltips_{this};
    std::unique_ptr<juce::DocumentWindow> settings_;
    ens::app::Snapshot snap_{};
    bool blinkOn_ = true, hexCodes_ = false, foreground_ = true;
    int ticks_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelEditor)
};
