#pragma once
// A placeholder editor until the front panel (Plan 4 Task 6): the display's two rows as text, or the
// reason the machine does not run, and a button that opens the settings page.
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

class TsProcessor;

class PanelEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PanelEditor(TsProcessor& p);
    ~PanelEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void openSettings();

    TsProcessor& proc_;
    juce::TextButton settingsButton_{"Settings"};
    std::unique_ptr<juce::DocumentWindow> settings_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelEditor)
};
