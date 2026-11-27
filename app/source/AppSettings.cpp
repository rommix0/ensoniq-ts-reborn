#include "AppSettings.h"

namespace {

juce::PropertiesFile::Options options() {
    juce::PropertiesFile::Options o;
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 0;  // save on every change
    return o;
}

}  // namespace

AppSettings::AppSettings() : props_(folder().getChildFile("settings.xml"), options()) {}

juce::File AppSettings::folder() {
    const auto env = juce::SystemStats::getEnvironmentVariable("TSR_SETTINGS_DIR", {});
    return env.isNotEmpty() ? juce::File(env)
                            : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("TS Reborn");
}

juce::File AppSettings::romFolder() const {
    const auto path = props_.getValue("romFolder");
    return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
}

void AppSettings::setRomFolder(const juce::File& dir) { props_.setValue("romFolder", dir.getFullPathName()); }

// DECISION A137: the model is stored as "ts10" / "ts12"; anything else is the TS-10.
ens::ts::Model AppSettings::model() const {
    return props_.getValue("model") == "ts12" ? ens::ts::Model::Ts12 : ens::ts::Model::Ts10;
}

void AppSettings::setModel(ens::ts::Model m) { props_.setValue("model", m == ens::ts::Model::Ts12 ? "ts12" : "ts10"); }

// DECISION A116: one memory file per model; the TS-10's key and default name are the old ones.
juce::File AppSettings::memoryFile(ens::ts::Model m) const {
    const bool t12 = m == ens::ts::Model::Ts12;
    const auto path = props_.getValue(t12 ? "memoryFile12" : "memoryFile");
    return juce::File::isAbsolutePath(path) ? juce::File(path)
                                            : folder().getChildFile(t12 ? "ts12-memory.bin" : "ts-memory.bin");
}

void AppSettings::setMemoryFile(ens::ts::Model m, const juce::File& file) {
    props_.setValue(m == ens::ts::Model::Ts12 ? "memoryFile12" : "memoryFile", file.getFullPathName());
}
