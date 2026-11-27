#pragma once
// The app's settings: %APPDATA%\TS Reborn\settings.xml (Linux: ~/.config/TS Reborn/), shared by the
// Standalone and VST3 builds (design section 8). Each change is saved at once.
#include <juce_data_structures/juce_data_structures.h>

#include "ts/panel.h"

class AppSettings {
public:
    AppSettings();
    // %APPDATA%\TS Reborn, or the folder in the environment variable TSR_SETTINGS_DIR when it is set
    // (checks that must not touch the user's own settings and memory).
    static juce::File folder();

    juce::File romFolder() const;  // juce::File() until one is chosen
    void setRomFolder(const juce::File& dir);
    // The instrument model (A137): key "model", "ts10" or "ts12"; default TS-10.
    ens::ts::Model model() const;
    void setModel(ens::ts::Model m);
    // Per model (A116): keys memoryFile / memoryFile12; default ts-memory.bin / ts12-memory.bin in folder().
    juce::File memoryFile(ens::ts::Model m) const;
    void setMemoryFile(ens::ts::Model m, const juce::File& file);

private:
    juce::PropertiesFile props_;
};
