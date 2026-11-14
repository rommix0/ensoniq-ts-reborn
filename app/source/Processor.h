#pragma once
// The plugin's AudioProcessor: owns the settings and the engine and plays it, Main and Aux out, MIDI in.
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "AppSettings.h"
#include "app/engine.h"

class TsProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    TsProcessor();
    ~TsProcessor() override;  // saves the memory (A116)

    ens::app::Engine* engine() { return engine_.get(); }  // null until the ROMs are good
    // Why there is no engine, two display rows separated by '\n'.
    juce::String romError() const { return romError_; }
    // Why the memory file was not loaded or not saved, or empty.
    juce::String memoryError() const { return memoryError_; }
    // The 30 Hz timer's work, public so the checks can call it: counts the 60 s save (A116).
    void tick();
    bool prepared() const { return rate_ > 0; }  // prepareToPlay has run: an audio device is open
    const AppSettings& settings() const { return settings_; }

    // Message thread. Each suspends processing around the engine swap itself.
    // Stores dir and, when all six ROMs are good, re-creates the machine keeping its memory.
    bool setRomFolder(const juce::File& dir, juce::String& err);
    // Stores file and, when it holds an image, restarts the machine from it. A file in the ROM
    // folder, one that cannot be written or one of the wrong size is refused (err) and the setting stays.
    bool setMemoryFile(const juce::File& file, juce::String& err);
    // Zeros to the file, then a cold machine (A116); the caller has confirmed. A failed write leaves the
    // machine running and sets memoryError.
    void eraseMemory();

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // The plugin state comes with Plan 4 Task 5 (A117); until then there is none.
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    // Keep: the running RAM once booted, else the start image. File: the memory file. Start: the start image.
    enum class Ram { Keep, File, Start };
    bool restart(Ram ram);  // a new engine from roms_; the old one stays if that fails
    std::vector<uint8_t> select(const ens::app::Snapshot& snap) const;  // the selection a restart keeps
    void timerCallback() override { tick(); }
    bool saved(bool ok);  // ok; a failure sets memoryError_, a success clears that

    AppSettings settings_;
    // The image the machine last started from and the selection to replay with it (A105); set by each restart.
    std::vector<uint8_t> startImage_, startSelect_;
    bool haveStart_ = false;
    ens::ts::RomSet roms_;  // the last good set; empty until then
    std::unique_ptr<ens::app::Engine> engine_;
    juce::String romError_, memoryError_;
    double rate_ = 0;  // 0 until prepareToPlay
    int block_ = 0;
    std::vector<float> auxL_, auxR_;  // Aux when its bus is off, added into Main (A109); block_ frames
    int ticks_ = 0;            // timer ticks (30 Hz) since the last save request
    bool ramPending_ = false;  // a requestRam is waiting for takeRam
    std::vector<uint8_t> ramBuf_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TsProcessor)
};
