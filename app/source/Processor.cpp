#include "Processor.h"

#include <algorithm>

#include "MemoryFile.h"
#include "PanelEditor.h"
#include "RomFolder.h"

namespace {

// The first file that is not good, as two display rows; empty when all six are good.
juce::String romProblem(const std::vector<RomFileStatus>& list) {
    for (const auto& s : list)
        if (s.state != RomFileStatus::Found)
            return juce::String(s.state == RomFileStatus::Missing ? "ROM missing:\n" : "ROM wrong size:\n") + s.file;
    return {};
}

// Why file cannot be the memory file, or empty. Not in the ROM folder (os/ts10_310l.bin is 512 KB
// too, and the 60 s save would overwrite it), and a file that can be written.
juce::String memoryInRomFolder(const juce::File& file, const juce::File& roms) {
    if (roms != juce::File() && file.isAChildOf(roms))
        return "The memory file cannot be in the ROM folder: " + file.getFullPathName();
    return {};
}

juce::String memoryTargetProblem(const juce::File& file, const juce::File& roms) {
    if (const auto err = memoryInRomFolder(file, roms); err.isNotEmpty()) return err;
    if (file.isDirectory() || !file.hasWriteAccess())  // a new file: its folder's access
        return "The memory file cannot be written: " + file.getFullPathName();
    return {};
}

}  // namespace

// DECISION A109: Main and Aux, stereo. The Standalone starts with Aux off, so it plays both on one pair.
TsProcessor::TsProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Main", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Aux", juce::AudioChannelSet::stereo(),
                                     juce::PluginHostType::getPluginLoadedAs() != wrapperType_Standalone)) {
    // No compiled-in default. With no folder chosen, a "roms" folder beside the app (the dist's
    // layout) is taken and stored, so the plugin finds it too; with neither, the first start opens
    // with no ROMs.
    auto dir = settings_.romFolder();
    if (dir == juce::File()) {
        const auto beside =
            juce::File::getSpecialLocation(juce::File::currentApplicationFile).getSiblingFile("roms");
        if (beside.isDirectory()) dir = beside;
    }
    if (dir != juce::File())
        setRomFolder(dir, romError_);
    else
        romError_ = "No ROM folder:\nopen Settings";
    startTimerHz(30);  // the 60 s save counts 1800 ticks (A116)
}

TsProcessor::~TsProcessor() {
    stopTimer();
    // DECISION A116: saved on close only once booted; audio has stopped, so the machine is read directly.
    if (engine_ && engine_->snapshot().booted) saveMemoryFile(settings_.memoryFile(), engine_->machine().ramImage());
}

// Refused, with the reason in err, when the memory file is inside the new folder: it would load the
// OS ROM as RAM and the 60 s save would overwrite it (setMemoryFile guards the other order).
bool TsProcessor::setRomFolder(const juce::File& dir, juce::String& err) {
    err = memoryInRomFolder(settings_.memoryFile(), dir);
    if (err.isNotEmpty()) return false;
    settings_.setRomFolder(dir);
    ens::ts::RomSet roms;
    const auto problem = romProblem(checkRomFolder(dir, &roms));
    if (problem.isNotEmpty()) {
        if (!engine_) romError_ = problem;
        return false;  // a running machine keeps running
    }
    roms_ = std::move(roms);
    return restart(Ram::Keep);
}

bool TsProcessor::setMemoryFile(const juce::File& file, juce::String& err) {
    err = memoryTargetProblem(file, settings_.romFolder());
    if (err.isNotEmpty()) return false;
    if (file.existsAsFile()) {
        loadMemoryFile(file, err);
        if (err.isNotEmpty()) return false;
    }
    settings_.setMemoryFile(file);
    memoryError_ = {};
    if (file.existsAsFile() && engine_) restart(Ram::File);
    return true;
}

// DECISION A116: erase writes the 512 KB zero image to the memory file at once (an explicit act, not
// held back by the boot gate) and restarts the machine from it, so the firmware rebuilds its memory as
// it boots. With no machine, only the file is erased. If the write fails, nothing is erased and the
// machine runs on; the settings page says so. The machine comes back cold on U0-0: no select to replay.
void TsProcessor::eraseMemory() {
    if (!saved(eraseMemoryFile(settings_.memoryFile()))) return;
    memoryError_ = {};
    startSelect_.clear();
    if (engine_) {
        startImage_.assign(static_cast<size_t>(kMemoryBytes), 0);
        haveStart_ = true;
        restart(Ram::Start);
    }
}

// A failed save is shown on the settings page (memoryError) until a save succeeds.
bool TsProcessor::saved(bool ok) {
    const auto msg = "Memory not saved: " + settings_.memoryFile().getFullPathName();
    if (!ok)
        memoryError_ = msg;
    else if (memoryError_ == msg)
        memoryError_ = {};
    return ok;
}

bool TsProcessor::restart(Ram ram) {
    suspendProcessing(true);  // from here the audio thread is out of processBlock, and stays out
    const auto snap = engine_ ? engine_->snapshot() : ens::app::Snapshot{};
    std::vector<uint8_t> img;
    // Start replays the start image's select; Keep and File keep the running one once booted (A105),
    // so a ROM folder or memory file change keeps the sound.
    auto sel = ram == Ram::Start ? startSelect_ : select(snap);
    if (ram == Ram::Keep && snap.booted) {
        img = engine_->machine().ramImage();  // audio stopped: the machine is ours to read
    } else if (ram != Ram::File && haveStart_) {
        img = startImage_;  // DECISION A116: never a half-booted image
    } else {
        img = loadMemoryFile(settings_.memoryFile(), memoryError_);  // wrong size: reported, cold boot
    }
    std::string e;
    auto fresh = ens::app::Engine::create(roms_, e);
    if (fresh && !img.empty() && !fresh->loadRam(img, e)) fresh.reset();
    const bool ok = fresh != nullptr;
    if (ok) {
        fresh->setSelect(sel.data(), static_cast<int>(sel.size()));
        if (rate_ > 0) fresh->prepare(rate_, block_);
        engine_.swap(fresh);  // fresh now holds the old engine, freed below after the resume
        startImage_ = std::move(img);
        startSelect_ = std::move(sel);
        haveStart_ = true;
        ramPending_ = false;
        ticks_ = 0;
        romError_ = {};
    } else if (!engine_) {
        romError_ = juce::String(e);
    }
    suspendProcessing(false);
    return ok;
}

// DECISION A116: every 60 s, once booted, the audio thread copies the RAM (requestRam) and the next
// tick that finds the copy (takeRam) saves it; 1800 ticks of the 30 Hz timer make the 60 s.
void TsProcessor::tick() {
    if (!engine_) return;
    if (ramPending_) {
        if (engine_->takeRam(ramBuf_)) {
            ramPending_ = false;
            saved(saveMemoryFile(settings_.memoryFile(), ramBuf_));
        }
    } else if (++ticks_ >= 1800 && engine_->snapshot().booted) {
        ticks_ = 0;
        engine_->requestRam();
        ramPending_ = true;
    }
}

void TsProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    rate_ = sampleRate;
    block_ = std::max(1, maximumExpectedSamplesPerBlock);
    auxL_.assign(static_cast<size_t>(block_), 0.0f);  // DECISION A109: Aux when its bus is off
    auxR_.assign(static_cast<size_t>(block_), 0.0f);
    if (engine_) engine_->prepare(rate_, block_);
}

// DECISION A109: Main stereo; Aux stereo or disabled.
bool TsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto aux = layouts.getChannelSet(false, 1);
    return layouts.inputBuses.isEmpty() && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           (aux.isDisabled() || aux == juce::AudioChannelSet::stereo());
}

void TsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer(buffer, false, 0);  // views of buffer's channels: no allocation
    auto aux = getBusBuffer(buffer, false, 1);
    if (!engine_ || main.getNumChannels() < 2 || auxL_.empty()) {  // no machine, or not prepared
        buffer.clear();
        return;
    }
    const int n = buffer.getNumSamples();
    float* const mainL = main.getWritePointer(0);
    float* const mainR = main.getWritePointer(1);
    if (aux.getNumChannels() >= 2) {
        // DECISION A108: every byte of every message, sysex and real-time included, as the MIDI IN jack.
        for (const auto m : midi) engine_->midi(m.samplePosition, m.data, m.numBytes);
        engine_->render(mainL, mainR, aux.getWritePointer(0), aux.getWritePointer(1), n);
        return;
    }
    // DECISION A109: Aux off: rendered into the scratch pair and added into Main. A block longer than
    // prepared goes in pieces of the scratch size, each with its own messages (A108).
    const int piece = static_cast<int>(auxL_.size());
    for (int done = 0; done < n; done += piece) {
        const int k = std::min(piece, n - done);
        for (const auto m : midi)
            if (const int at = std::clamp(m.samplePosition, 0, n - 1); at / piece == done / piece)
                engine_->midi(at - done, m.data, m.numBytes);
        engine_->render(mainL + done, mainR + done, auxL_.data(), auxR_.data(), k);
        juce::FloatVectorOperations::add(mainL + done, auxL_.data(), k);
        juce::FloatVectorOperations::add(mainR + done, auxR_.data(), k);
    }
}

// The selection a Keep or File restart keeps (A105): the running one once booted, else the one the
// start image came with.
std::vector<uint8_t> TsProcessor::select(const ens::app::Snapshot& snap) const {
    if (snap.booted && snap.selectLen > 0) return {snap.select.begin(), snap.select.begin() + snap.selectLen};
    return startSelect_;
}

juce::AudioProcessorEditor* TsProcessor::createEditor() { return new PanelEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TsProcessor(); }
