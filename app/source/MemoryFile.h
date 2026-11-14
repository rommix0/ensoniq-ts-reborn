#pragma once
// The battery memory file: the machine's 512 KB RAM image, raw (T13). The rules of when to save are
// the processor's (A116).
#include <juce_core/juce_core.h>

#include <vector>

constexpr juce::int64 kMemoryBytes = 0x80000;  // OSRAM and SEQEXP, Machine::ramImage() (T13)

// Empty when the file does not exist (a cold boot). A file of the wrong size gives empty and err.
std::vector<uint8_t> loadMemoryFile(const juce::File& file, juce::String& err);
// Writes <file>.tmp, then moves it over file. False if either step fails.
bool saveMemoryFile(const juce::File& file, const std::vector<uint8_t>& img);
// Erase: saves the 512 KB zero image (A116).
inline bool eraseMemoryFile(const juce::File& file) {
    return saveMemoryFile(file, std::vector<uint8_t>(static_cast<size_t>(kMemoryBytes), 0));
}
