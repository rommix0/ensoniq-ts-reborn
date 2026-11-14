#include "PanelEditor.h"

#include "Processor.h"
#include "SettingsPage.h"
#include "ts/panel.h"

namespace {

const juce::Colour kPanel{0xff1f2023}, kLcd{0xffffb347}, kLcdBack{0xff050403};

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

}  // namespace

PanelEditor::PanelEditor(TsProcessor& p) : AudioProcessorEditor(p), proc_(p) {
    addAndMakeVisible(settingsButton_);
    settingsButton_.onClick = [this] { openSettings(); };
    setSize(640, 160);
    startTimerHz(30);
}

PanelEditor::~PanelEditor() = default;

void PanelEditor::resized() { settingsButton_.setBounds(getWidth() - 110, getHeight() - 40, 100, 30); }

void PanelEditor::paint(juce::Graphics& g) {
    g.fillAll(kPanel);
    juce::String rows;
    if (auto* e = proc_.engine()) {
        const auto panel = e->snapshot().panel;
        auto text = ens::ts::rowText(panel, 0) + "\n" + ens::ts::rowText(panel, 1);
        for (auto& c : text)
            if (const auto u = static_cast<unsigned char>(c); (u < 0x20 || u > 0x7E) && c != '\n') c = ' ';  // A113's rule, ahead of Task 6's display
        rows = proc_.prepared() ? juce::String(text) : juce::String("No audio device");
    } else {
        rows = proc_.romError();
    }
    const auto lcd = getLocalBounds().reduced(10).withTrimmedBottom(40);
    g.setColour(kLcdBack);
    g.fillRect(lcd);
    g.setColour(kLcd);
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::plain));
    g.drawFittedText(rows, lcd.reduced(10), juce::Justification::centredLeft, 2);
}

void PanelEditor::openSettings() {
    if (settings_ == nullptr) {
        auto page = std::make_unique<SettingsPage>(proc_);
        page->onChange = [this] { repaint(); };
        settings_ = std::make_unique<SettingsWindow>(std::move(page));
        settings_->centreAroundComponent(this, settings_->getWidth(), settings_->getHeight());
    }
    settings_->setVisible(true);
    settings_->toFront(true);
}
