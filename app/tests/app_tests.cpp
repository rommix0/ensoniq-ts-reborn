// TS Reborn app checks (from KS-32 Reborn's): checkRomFolder on a temp folder of dummy files the right
// size (never ROM content), the memory file's load and save, and the processor's memory-file guards. On
// the real ROMs: the two output buses (A109), and the built TS Reborn.vst3 loaded and played through
// JUCE's own VST3 host, as a DAW would. Every check sets TSR_SETTINGS_DIR to a temp folder.
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../source/AppSettings.h"
#include "../source/MemoryFile.h"
#include "../source/Processor.h"
#include "../source/RomFolder.h"

namespace {

int failures = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                              \
        }                                                            \
    } while (0)

// Points AppSettings (and a loaded plugin, which shares this process's environment) at a test folder.
void setSettingsDir(const juce::File& dir) {
#ifdef _WIN32
    _putenv_s("TSR_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
#else
    setenv("TSR_SETTINGS_DIR", dir.getFullPathName().toRawUTF8(), 1);
#endif
}

void write(const juce::File& f, size_t size) {
    f.getParentDirectory().createDirectory();
    const std::vector<char> zeros(size);
    f.replaceWithData(zeros.data(), zeros.size());
}

int count(const std::vector<RomFileStatus>& list, RomFileStatus::State s) {
    int n = 0;
    for (const auto& r : list) n += r.state == s;
    return n;
}

// The six ROM files as zeros of the right sizes.
void dummyRoms(const juce::File& dir) {
    const std::pair<const char*, size_t> files[] = {
        {"os/ts10_310h.bin", 0x40000},       {"os/ts10_310l.bin", 0x80000},       {"waves/901401_u41.bin", 0x200000},
        {"waves/901301_u43.bin", 0x100000}, {"waves/901402_u45.bin", 0x200000}, {"waves/901302_u46.bin", 0x100000}};
    for (const auto& [name, size] : files) write(dir.getChildFile(name), size);
}

bool haveRoms() {
    if (juce::File(TSR_ROM_DIR).getChildFile("waves/901401_u41.bin").existsAsFile()) return true;
    std::printf("  skip: ROMs not found\n");
    return false;
}

void romFolder(const juce::File& dir) {
    dummyRoms(dir);

    ens::ts::RomSet roms;
    auto list = checkRomFolder(dir, &roms);
    CHECK(list.size() == 6 && count(list, RomFileStatus::Found) == 6);
    std::string err;
    CHECK(ens::ts::Machine::create(roms, err) != nullptr);  // the sizes are the ones create takes

    // The 256 KB low OS image Machine::create also takes.
    write(dir.getChildFile("os/ts10_310l.bin"), 0x40000);
    ens::ts::RomSet half;
    list = checkRomFolder(dir, &half);
    CHECK(count(list, RomFileStatus::Found) == 6 && half.osLow.size() == 0x40000);
    CHECK(ens::ts::Machine::create(half, err) != nullptr);

    // A renamed wave ROM: missing, and no RomSet.
    CHECK(dir.getChildFile("waves/901302_u46.bin").moveFileTo(dir.getChildFile("waves/901302_u46.bak")));
    ens::ts::RomSet none;
    list = checkRomFolder(dir, &none);
    CHECK(list[5].file == "waves/901302_u46.bin" && list[5].state == RomFileStatus::Missing);
    CHECK(count(list, RomFileStatus::Found) == 5 && none.osHigh.empty() && none.u41.empty());

    write(dir.getChildFile("waves/901402_u45.bin"), 100);  // a short file
    list = checkRomFolder(dir, nullptr);
    CHECK(list[4].state == RomFileStatus::WrongSize);
    write(dir.getChildFile("os/ts10_310h.bin"), 0x80000);  // the high OS image has one size only
    CHECK(checkRomFolder(dir, nullptr)[0].state == RomFileStatus::WrongSize);

    CHECK(count(checkRomFolder(juce::File(), nullptr), RomFileStatus::Missing) == 6);  // no folder chosen
}

void memoryFile(const juce::File& dir) {
    const auto mem = dir.getChildFile("mem/ts-memory.bin");
    juce::String err;
    CHECK(loadMemoryFile(mem, err).empty() && err.isEmpty());  // none yet: a cold boot, no error

    std::vector<uint8_t> img(static_cast<size_t>(kMemoryBytes));
    img.front() = 1;
    img.back() = 2;
    CHECK(saveMemoryFile(mem, img));
    CHECK(loadMemoryFile(mem, err) == img && err.isEmpty());
    img[5] = 9;
    CHECK(saveMemoryFile(mem, img));  // over an existing file
    CHECK(loadMemoryFile(mem, err) == img);
    CHECK(!mem.getSiblingFile("ts-memory.bin.tmp").exists());

    // Erase (A116): the file becomes 512 KB of zeros at once, over the saved image.
    CHECK(eraseMemoryFile(mem));
    const auto erased = loadMemoryFile(mem, err);
    CHECK(err.isEmpty() && erased.size() == 0x80000);
    CHECK(std::all_of(erased.begin(), erased.end(), [](uint8_t b) { return b == 0; }));

    mem.replaceWithData(img.data(), 100);  // the wrong size: reported and ignored
    CHECK(loadMemoryFile(mem, err).empty() && err.isNotEmpty());
}

// The processor's memory-file guards, on dummy ROMs (a machine is made but never run) in a temp
// settings folder.
void processor(const juce::File& dir) {
    const auto roms = dir.getChildFile("roms");
    dummyRoms(roms);
    setSettingsDir(dir);
    AppSettings().setRomFolder(roms);
    TsProcessor p;
    CHECK(p.engine() != nullptr);
    CHECK(p.settings().memoryFile() == dir.getChildFile("ts-memory.bin"));

    // A ROM is never the memory file, though os/ts10_310l.bin is 512 KB: the 60 s save would overwrite it.
    juce::String err;
    const auto low = roms.getChildFile("os/ts10_310l.bin");
    CHECK(!p.setMemoryFile(low, err) && err.isNotEmpty() && p.settings().memoryFile() != low);

    // The other order: a memory file chosen first, then a ROM folder that contains it, is refused;
    // the file and the ROM folder setting are untouched.
    const auto dump = dir.getChildFile("dump"), inDump = dump.getChildFile("m.bin");
    CHECK(eraseMemoryFile(inDump));
    CHECK(p.setMemoryFile(inDump, err) && p.settings().memoryFile() == inDump);
    const auto* engine0 = p.engine();
    CHECK(!p.setRomFolder(dump, err) && err.isNotEmpty() && p.settings().romFolder() == roms &&
          p.engine() == engine0 && inDump.getSize() == kMemoryBytes);
    CHECK(p.setMemoryFile(dir.getChildFile("mem0.bin"), err));  // back to a file outside the dump

    // A file that cannot be written is refused.
    const auto mem = dir.getChildFile("mem.bin");
    CHECK(eraseMemoryFile(mem) && mem.setReadOnly(true));
    CHECK(!p.setMemoryFile(mem, err) && err.isNotEmpty() && p.settings().memoryFile() != mem);

    // Taken while writable (a restart from it). Then, read-only, Erase fails: the memory error says
    // so and the machine is not restarted.
    CHECK(mem.setReadOnly(false) && p.setMemoryFile(mem, err) && p.memoryError().isEmpty());
    const auto* engine = p.engine();
    CHECK(mem.setReadOnly(true));
    p.eraseMemory();
    CHECK(p.memoryError().startsWith("Memory not saved") && p.engine() == engine);
    // Writable again: Erase saves, clears the error and restarts.
    CHECK(mem.setReadOnly(false));
    p.eraseMemory();
    CHECK(p.memoryError().isEmpty() && p.engine() != engine);

    // A memory file of the wrong size is refused and the setting stays.
    const auto shortFile = dir.getChildFile("short.bin");
    write(shortFile, 0x10000);
    CHECK(!p.setMemoryFile(shortFile, err) && err.isNotEmpty() && p.settings().memoryFile() == mem);
}

// rom_folder_beside_app: with no folder chosen, a "roms" folder beside the app is taken and stored
// (the dist layout); a chosen folder wins over it.
void romsBesideApp(const juce::File& dir) {
    const auto beside = juce::File::getSpecialLocation(juce::File::currentApplicationFile).getSiblingFile("roms");
    CHECK(!beside.exists());  // never a real folder of the user's
    if (beside.exists()) return;
    dummyRoms(beside);
    setSettingsDir(dir);
    {
        TsProcessor p;
        CHECK(p.engine() != nullptr && p.settings().romFolder() == beside);
    }
    const auto chosen = dir.getChildFile("chosen");
    dummyRoms(chosen);
    AppSettings().setRomFolder(chosen);
    {
        TsProcessor p;
        CHECK(p.settings().romFolder() == chosen);
    }
    beside.deleteRecursively();
}

// processor_aux_bus_off_mixes_into_main (A109): two processors get the same blocks and, after the boot,
// 90 3C 7F; the one with Aux disabled plays on Main exactly the other's Main + Aux. A 1000-frame block,
// over the 480 prepared, takes the Aux-off path's pieces.
void auxBusOff(const juce::File& dir) {
    if (!haveRoms()) return;
    setSettingsDir(dir);
    AppSettings().setRomFolder(juce::File(TSR_ROM_DIR));
    TsProcessor off, on;
    CHECK(on.getBusCount(false) == 2 && on.getTotalNumOutputChannels() == 4);  // both enabled outside the Standalone
    CHECK(off.disableNonMainBuses() && off.getTotalNumOutputChannels() == 2);
    CHECK(off.engine() != nullptr && on.engine() != nullptr);
    if (!off.engine()) std::printf("  rom error: %s\n", off.romError().toRawUTF8());
    if (!off.engine() || !on.engine() || off.getTotalNumOutputChannels() != 2) return;
    off.prepareToPlay(48000, 480);
    on.prepareToPlay(48000, 480);

    int mismatches = 0;
    float mainPeak = 0, auxPeak = 0;
    const auto block = [&](int frames, bool note) {
        juce::AudioBuffer<float> a(2, frames), b(4, frames);
        a.clear();
        b.clear();
        juce::MidiBuffer ma, mb;
        if (note) {
            const uint8_t on3c[] = {0x90, 0x3C, 0x7F};
            ma.addEvent(on3c, 3, 0);
            mb.addEvent(on3c, 3, 0);
        }
        off.processBlock(a, ma);
        on.processBlock(b, mb);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c) {
                mismatches += a.getSample(c, i) != b.getSample(c, i) + b.getSample(c + 2, i);
                mainPeak = std::max(mainPeak, std::abs(b.getSample(c, i)));
                auxPeak = std::max(auxPeak, std::abs(b.getSample(c + 2, i)));
            }
    };
    for (int i = 0; i < 550; ++i) block(480, false);  // the boot: kBootMs 5300 plus a margin
    CHECK(off.engine()->snapshot().booted && on.engine()->snapshot().booted);
    block(480, true);
    for (int i = 0; i < 49; ++i) block(480, false);  // 500 ms from the note
    block(1000, false);
    std::printf("  aux bus: mismatches %d, Main peak %.4f, Aux peak %.4f\n", mismatches, mainPeak, auxPeak);
    CHECK(mismatches == 0 && mainPeak > 0.001f);
}

// The built VST3 through JUCE's VST3 host: TSR_SETTINGS_DIR points the plugin at a temp settings file
// naming the ROM folder, so neither the user's settings nor their memory file is touched. Aux is off,
// as the Standalone plays it (A109).
void vst3(const juce::File& dir) {
    const juce::File bundle(TSR_VST3_PATH);
    if (!bundle.exists()) {
        std::printf("  skip: VST3 not found\n");
        return;
    }
    if (!haveRoms()) return;
    setSettingsDir(dir);  // the plugin DLL reads this process's environment, so it lands here too
    AppSettings().setRomFolder(juce::File(TSR_ROM_DIR));

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, bundle.getFullPathName());
    CHECK(types.size() == 1);
    if (types.isEmpty()) return;
    CHECK(types[0]->name == "TS Reborn" && types[0]->isInstrument);

    constexpr double kRate = 48000;
    constexpr int kBlock = 480;  // 10 ms
    juce::String err;
    auto plugin = format.createInstanceFromDescription(*types[0], kRate, kBlock, err);
    CHECK(plugin != nullptr);
    if (!plugin) {
        std::printf("  load failed: %s\n", err.toRawUTF8());
        return;
    }
    CHECK(plugin->getBusCount(false) == 2 && plugin->disableNonMainBuses() &&
          plugin->getTotalNumOutputChannels() == 2);
    plugin->prepareToPlay(kRate, kBlock);

    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    float l = 0, r = 0;
    for (int b = 0; b < 650; ++b) {  // the boot (5500 ms), then a note for 1000 ms
        if (b == 550) midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        buf.clear();
        plugin->processBlock(buf, midi);
        midi.clear();
        if (b >= 550) {
            l = std::max(l, buf.getMagnitude(0, 0, kBlock));
            r = std::max(r, buf.getMagnitude(1, 0, kBlock));
        }
    }
    std::printf("  VST3 note peak: %.4f %.4f\n", l, r);
    CHECK(l > 0.001f && r > 0.001f);
    plugin->releaseResources();
    plugin.reset();
}

}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI gui;  // the VST3 host needs the message manager
    const auto dir =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("tsr_app_tests", "", false);
    romFolder(dir.getChildFile("roms"));
    memoryFile(dir);
    processor(dir.getChildFile("proc"));
    romsBesideApp(dir.getChildFile("beside"));
    auxBusOff(dir.getChildFile("buses"));  // not "aux": a reserved device name on Windows
    vst3(dir.getChildFile("vst3"));
    dir.deleteRecursively();
    std::printf(failures == 0 ? "app_tests: all passed\n" : "app_tests: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
