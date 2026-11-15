// The Standalone's application (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP): JUCE's own StandaloneFilterApp
// (juce_audio_plugin_client_Standalone.cpp, JUCE 8.0.16) with one change, the close asks about an unsaved
// disk first (A120). Same settings file and window as JUCE's, so nothing the user set moves.
#include "Processor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace {

// DECISION A120: closing the window (or the system asking the app to quit) goes through the drive's prompt
// when the disk has unsaved writes; Cancel keeps the app open. A close while a dialog is up is ignored: Windows
// blocks it behind the modal dialog, Linux does not (juce_XWindowSystem_linux.cpp hands it to the window).
class TsWindow final : public juce::StandaloneFilterWindow {
public:
    using StandaloneFilterWindow::StandaloneFilterWindow;

    void closeButtonPressed() override {
        if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0) return;
        auto* p = dynamic_cast<TsProcessor*>(pluginHolder->processor.get());
        if (p == nullptr) {
            quitNow();
            return;
        }
        p->drive().askThen([this] { quitNow(); });
    }

private:
    void quitNow() {
        pluginHolder->savePluginState();
        juce::JUCEApplicationBase::quit();
    }
};

class TsApp final : public juce::JUCEApplication {
public:
    TsApp() {
        juce::PropertiesFile::Options options;
        options.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
#else
        options.folderName = "";
#endif
        appProperties_.setStorageParameters(options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8(JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) {  // no display: audio only, as JUCE's
            holder_ = holder();
            return;
        }
        window_ = std::make_unique<TsWindow>(
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
            holder());
        window_->setVisible(true);
    }

    void shutdown() override {
        holder_ = nullptr;
        window_ = nullptr;
        appProperties_.saveIfNeeded();
    }

    // As JUCE's: open dialogs are cancelled first and the request comes back 100 ms later; then the close
    // (with its prompt, A120).
    void systemRequestedQuit() override {
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            juce::Timer::callAfterDelay(100, [] {
                if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
            });
            return;
        }
        if (window_ != nullptr) {
            window_->closeButtonPressed();
            return;
        }
        if (holder_ != nullptr) holder_->savePluginState();
        quit();
    }

private:
    std::unique_ptr<juce::StandalonePluginHolder> holder() {
        return std::make_unique<juce::StandalonePluginHolder>(appProperties_.getUserSettings(), false);
    }

    juce::ApplicationProperties appProperties_;
    std::unique_ptr<TsWindow> window_;
    std::unique_ptr<juce::StandalonePluginHolder> holder_;
};

}  // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new TsApp(); }
