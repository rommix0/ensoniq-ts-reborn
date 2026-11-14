// TS Reborn app checks (from KS-32 Reborn's): checkRomFolder on a temp folder of dummy files the right
// size (never ROM content), the memory file's load and save, the processor's memory-file guards and its
// parameters (A111). On the real ROMs: the sound follow (A111), the plugin state and memory per format
// (A117), the two output buses (A109), and the built TS Reborn.vst3 loaded and played through JUCE's own
// VST3 host, as a DAW would. Every check sets TSR_SETTINGS_DIR to a temp folder.
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "../source/AppSettings.h"
#include "../source/MemoryFile.h"
#include "../source/Processor.h"
#include "../source/RomFolder.h"
#include "app/analog_map.h"
#include "ts/panel.h"

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
    TsProcessor p(MemoryMode::File);  // the memory file's own rules (A116)
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

// parameters (A111): six host parameters, by ID and name; the defaults are the analogs' rest values.
void parameters(const juce::File& dir) {
    const auto roms = dir.getChildFile("roms");
    dummyRoms(roms);
    setSettingsDir(dir);
    AppSettings().setRomFolder(roms);
    TsProcessor p;
    CHECK(p.getParameters().size() == 6);
    const std::pair<const char*, const char*> expect[] = {
        {"volume", "Volume"},      {"dataEntry", "Data Entry"}, {"pitchWheel", "Pitch Wheel"},
        {"modWheel", "Mod Wheel"}, {"cvPedal", "CV Pedal"},     {"sound", "Sound"}};
    for (const auto& [id, name] : expect) {
        auto* q = p.params().getParameter(id);
        CHECK(q != nullptr && q->getName(64) == name);
    }
    auto* sound = p.params().getParameter("sound");
    CHECK(sound != nullptr && sound->convertFrom0to1(0.0f) == 0.0f && sound->convertFrom0to1(1.0f) == 127.0f);
    using ens::ts::Analog;
    const std::pair<Analog, const char*> analogs[] = {{Analog::Volume, "volume"},
                                                      {Analog::DataEntry, "dataEntry"},
                                                      {Analog::PitchWheel, "pitchWheel"},
                                                      {Analog::ModWheel, "modWheel"},
                                                      {Analog::CvPedal, "cvPedal"}};
    for (const auto& [a, id] : analogs)
        CHECK(ens::app::analogRaw(a, *p.params().getRawParameterValue(id)) ==
              ens::ts::kAnalogRest[static_cast<size_t>(a)]);
}

// The state checks (A117) on the real ROMs, with 10 ms blocks and no tick(), so `sound` has not
// followed a pick: a state taken before the 30 Hz timer caught up.
constexpr int kBoot = 5400;      // kBootMs 5300 plus a margin
constexpr int kRestored = 5900;  // a restored boot and the select's replay

bool realRoms(const juce::File& dir, const juce::File& romDir = juce::File(TSR_ROM_DIR)) {
    if (!haveRoms()) return false;
    setSettingsDir(dir);
    AppSettings().setRomFolder(romDir);
    return true;
}

void run(TsProcessor& p, int ms, juce::MidiBuffer midi = {}) {
    juce::AudioBuffer<float> buf(4, 480);
    for (int i = 0; i < ms / 10; ++i) {
        buf.clear();
        p.processBlock(buf, midi);
        midi.clear();
    }
}

void press(TsProcessor& p, ens::ts::Button b) {  // held 100 ms, then 300 ms for the display
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), true, 0});
    run(p, 100);
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), false, 0});
    run(p, 300);
}

juce::MidiBuffer programChange(int n) {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage(0xC0, n), 0);
    return m;
}

bool rowStarts(TsProcessor& p, int r, const char* text) {
    return p.engine() && ens::ts::rowText(p.engine()->snapshot().panel, r).rfind(text, 0) == 0;
}

juce::MemoryBlock stateOf(juce::AudioProcessor& p) {
    juce::MemoryBlock m;
    p.getStateInformation(m);
    return m;
}

std::unique_ptr<juce::XmlElement> xmlOf(const juce::MemoryBlock& s) {
    return juce::AudioProcessor::getXmlFromBinary(s.getData(), static_cast<int>(s.getSize()));
}

std::vector<uint8_t> ramOf(const juce::MemoryBlock& s) {
    const auto xml = xmlOf(s);
    const auto* ram = xml ? xml->getChildByName("RAM") : nullptr;
    if (!ram) return {};
    juce::MemoryOutputStream out;
    if (!juce::Base64::convertFromBase64(out, ram->getAllSubText())) return {};
    const auto* b = static_cast<const uint8_t*>(out.getData());
    return {b, b + out.getDataSize()};
}

juce::String selectOf(const juce::MemoryBlock& s) {
    const auto xml = xmlOf(s);
    return xml ? xml->getStringAttribute("select") : juce::String();
}

juce::MemoryBlock fileBytes(const juce::File& f) {
    juce::MemoryBlock m;
    f.loadFileAsData(m);  // empty when there is no file
    return m;
}

int soundOf(TsProcessor& p) {
    auto* q = p.params().getParameter("sound");
    return juce::roundToInt(q->convertFrom0to1(q->getValue()));
}

// state_round_trip_project: TUNE=+02, U0-5 by MIDI (C0 20) and Volume 0.5 come back in a new instance
// from the state alone. Returns A's state for the checks after it (empty when skipped).
juce::MemoryBlock stateRoundTrip(const juce::File& dir) {
    if (!realRoms(dir)) return {};
    using ens::ts::Button;
    TsProcessor a;
    CHECK(a.memoryMode() == MemoryMode::Project);  // constructed outside a wrapper
    if (!a.engine()) return {};
    a.prepareToPlay(48000, 480);
    run(a, kBoot);
    press(a, Button::System);
    press(a, Button::Up);
    press(a, Button::Up);
    CHECK(rowStarts(a, 0, "SYSTEM  TUNE=+02"));
    press(a, Button::Sounds);
    run(a, 200, programChange(0x20));
    a.params().getParameter("volume")->setValueNotifyingHost(0.5f);
    run(a, 500);
    CHECK(rowStarts(a, 0, "U0-5"));
    const auto s = stateOf(a);
    std::printf("  state: %d bytes, select %s\n", static_cast<int>(s.getSize()), selectOf(s).toRawUTF8());
    CHECK(ramOf(s).size() == static_cast<size_t>(kMemoryBytes) && selectOf(s).endsWithIgnoreCase("c020"));

    TsProcessor b;
    b.prepareToPlay(48000, 480);
    b.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
    run(b, kRestored);
    CHECK(b.engine() != nullptr && rowStarts(b, 0, "U0-5"));
    if (!b.engine()) return s;
    using ens::ts::Analog;
    CHECK(*b.params().getRawParameterValue("volume") == 0.5f &&
          b.engine()->machine().analog(Analog::Volume) == ens::app::analogRaw(Analog::Volume, 0.5f));
    press(b, Button::System);
    CHECK(rowStarts(b, 0, "SYSTEM  TUNE=+02"));
    return s;
}

// state_saved_during_boot_keeps_start_image (never a half-booted image).
void stateDuringBoot(const juce::File& dir, const juce::MemoryBlock& a) {
    if (a.isEmpty() || !realRoms(dir)) return;
    TsProcessor b;
    b.prepareToPlay(48000, 480);
    b.setStateInformation(a.getData(), static_cast<int>(a.getSize()));
    run(b, 1000);
    const auto s = stateOf(b);
    CHECK(ramOf(s) == ramOf(a) && selectOf(s) == selectOf(a));
}

// state_without_roms_round_trips (never lose a project's memory).
void stateWithoutRoms(const juce::File& dir, const juce::MemoryBlock& a) {
    const auto empty = dir.getChildFile("no-roms");
    empty.createDirectory();
    if (a.isEmpty() || !realRoms(dir, empty)) return;
    TsProcessor p;
    CHECK(p.engine() == nullptr);
    p.prepareToPlay(48000, 480);
    p.setStateInformation(a.getData(), static_cast<int>(a.getSize()));
    const auto s = stateOf(p);
    CHECK(ramOf(s) == ramOf(a) && selectOf(s) == selectOf(a));
    juce::String err;
    CHECK(p.setRomFolder(juce::File(TSR_ROM_DIR), err) && p.engine() != nullptr);
    if (!p.engine()) return;
    run(p, kRestored);
    CHECK(rowStarts(p, 0, "U0-5"));
}

// state_rejects_bad_blobs: garbage, a short RAM and an unknown version leave the machine as it is.
void stateBadBlobs(const juce::File& dir) {
    if (!realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, kBoot);
    const auto blob = [](const char* version, size_t ramBytes) {
        juce::XmlElement x("TSState");
        x.setAttribute("version", version);
        const std::vector<uint8_t> ram(ramBytes, 0x55);
        x.createNewChildElement("RAM")->addTextElement(juce::Base64::toBase64(ram.data(), ram.size()));
        juce::MemoryBlock m;
        juce::AudioProcessor::copyXmlToBinary(x, m);
        return m;
    };
    juce::MemoryBlock noise(100);
    juce::Random rng(8);
    for (size_t i = 0; i < noise.getSize(); ++i) noise[i] = static_cast<char>(rng.nextInt(256));
    for (const auto& s : {noise, blob("1", 1000), blob("2", static_cast<size_t>(kMemoryBytes))}) {
        const auto* engine = p.engine();
        p.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
        CHECK(p.engine() == engine);
        run(p, 100);
    }
}

// project_mode_leaves_shared_file (A117): no 60 s save, no close save, Erase spares the file; only
// copyToSharedMemory writes it, once booted.
void projectLeavesSharedFile(const juce::File& dir) {
    if (!realRoms(dir)) return;
    const auto shared = AppSettings().memoryFile();
    CHECK(eraseMemoryFile(shared));
    const auto before = fileBytes(shared);
    {
        TsProcessor p;
        if (!p.engine()) return;
        p.prepareToPlay(48000, 480);
        run(p, kBoot);
        for (int i = 0; i < 1900; ++i) p.tick();  // past the 60 s request; a block, then a tick would save
        run(p, 20);
        p.tick();
        const auto* engine = p.engine();
        p.eraseMemory();
        CHECK(p.engine() != engine && p.memoryError().isEmpty());
        CHECK(fileBytes(shared) == before);
    }
    {
        TsProcessor p;
        p.prepareToPlay(48000, 480);
        run(p, kBoot);
    }
    CHECK(fileBytes(shared) == before);
    TsProcessor p;
    p.prepareToPlay(48000, 480);
    juce::String err;
    CHECK(!p.copyToSharedMemory(err) && err.isNotEmpty());  // not booted
    run(p, kBoot);
    CHECK(p.copyToSharedMemory(err));
    const auto& ram = p.engine()->machine().ramImage();
    CHECK(fileBytes(shared) == juce::MemoryBlock(ram.data(), ram.size()));
}

// A117 (as KS-32 A21) across threads: processBlock runs on its own thread while saves come from this
// thread, a third thread and the audio thread itself. Each holds the running RAM with TUNE=+02, set
// after the boot, never the start image (512 KB of zeros here).
void stateThreads(const juce::File& dir) {
    if (!realRoms(dir)) return;
    CHECK(eraseMemoryFile(AppSettings().memoryFile()));
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    std::atomic<bool> stop{false}, onAudio{false};
    juce::MemoryBlock fromAudio;
    std::thread audio([&] {
        juce::AudioBuffer<float> buf(4, 480);
        juce::MidiBuffer midi;
        while (!stop) {
            buf.clear();
            p.processBlock(buf, midi);
            if (onAudio.load()) {
                fromAudio = stateOf(p);
                onAudio = false;
            }
        }
    });
    const auto wait = [&](int ms) {  // machine time
        const auto until = p.engine()->snapshot().frames + static_cast<uint64_t>(ms * ens::app::kFrameHz / 1000);
        while (p.engine()->snapshot().frames < until) juce::Thread::sleep(1);
    };
    const auto hold = [&](ens::ts::Button b) {
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), true, 0});
        wait(100);
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), false, 0});
        wait(300);
    };
    wait(kBoot);
    hold(ens::ts::Button::System);
    hold(ens::ts::Button::Up);
    hold(ens::ts::Button::Up);
    CHECK(rowStarts(p, 0, "SYSTEM  TUNE=+02"));
    const auto fromThis = stateOf(p);
    juce::MemoryBlock fromOther;
    std::thread([&] { fromOther = stateOf(p); }).join();
    onAudio = true;
    while (onAudio.load()) juce::Thread::sleep(1);
    stop = true;
    audio.join();

    const std::vector<uint8_t> zeros(static_cast<size_t>(kMemoryBytes), 0);
    const juce::MemoryBlock* states[] = {&fromThis, &fromOther, &fromAudio};
    for (const auto* s : states) {
        const auto ram = ramOf(*s);
        CHECK(ram.size() == static_cast<size_t>(kMemoryBytes) && ram != zeros);
        TsProcessor q;
        q.prepareToPlay(48000, 480);
        q.setStateInformation(s->getData(), static_cast<int>(s->getSize()));
        run(q, kRestored);
        press(q, ens::ts::Button::System);
        CHECK(rowStarts(q, 0, "SYSTEM  TUNE=+02"));
    }
}

// standalone_state_has_no_ram (A117, as KS-32 A23): the memory file is the Standalone's only memory.
// Both start from a memory file, so each has a start image; only the Project state carries it.
void standaloneNoRam(const juce::File& dir) {
    const auto roms = dir.getChildFile("roms");
    dummyRoms(roms);
    setSettingsDir(dir);
    AppSettings().setRomFolder(roms);
    CHECK(eraseMemoryFile(AppSettings().memoryFile()));
    TsProcessor p(MemoryMode::File), project;
    CHECK(p.memoryMode() == MemoryMode::File);
    const auto xml = xmlOf(stateOf(p));
    CHECK(xml != nullptr && xml->hasTagName("TSState") && xml->getChildByName("RAM") == nullptr);
    CHECK(ramOf(stateOf(project)).size() == static_cast<size_t>(kMemoryBytes));
}

// sound_follows_panel (A111): a panel pick of Soft2 (U0-1, program 1) reaches `sound` at the next tick,
// and the block that takes it adopts it: nothing is sent back to the machine.
void soundFollowsPanel(const juce::File& dir) {
    if (!realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, kBoot);
    p.tick();
    CHECK(soundOf(p) == 0);
    press(p, ens::ts::Button::Soft2);
    p.tick();
    CHECK(soundOf(p) == 1);
    run(p, 200);
    const auto snap = p.engine()->snapshot();
    CHECK(snap.program == 1 && snap.programsSent == 0);
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

// processor_aux_bus_off_mixes_into_main (A109): two processors get the same blocks. After the boot the
// running OS routes track 1 to the Aux outputs (Track Effects, Effects Bus Routing stepped with Up from
// VOICE to -AUX-), then both get 90 3C 7F. The one with Aux disabled plays on Main exactly the other's
// Main + Aux, and Aux sounds on both sides. A 1000-frame block, over the 480 prepared, takes the Aux-off
// path's pieces.
void auxBusOff(const juce::File& dir) {
    if (!haveRoms()) return;
    setSettingsDir(dir);
    AppSettings().setRomFolder(juce::File(TSR_ROM_DIR));
    TsProcessor off, on;
    CHECK(on.getBusCount(false) == 2 && on.getTotalNumOutputChannels() == 4);  // both enabled outside the Standalone
    CHECK(off.disableNonMainBuses() && off.getTotalNumOutputChannels() == 2);
    CHECK(off.engine() != nullptr && on.engine() != nullptr);
    if (!off.engine() || !on.engine() || off.getTotalNumOutputChannels() != 2) return;
    off.prepareToPlay(48000, 480);
    on.prepareToPlay(48000, 480);

    int mismatches = 0;
    float mainPeak = 0, auxPeak[2] = {0, 0};
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
                auxPeak[c] = std::max(auxPeak[c], std::abs(b.getSample(c + 2, i)));
            }
    };
    const auto press = [&](ens::ts::Button button) {  // held 100 ms, then 300 ms for the display
        for (bool down : {true, false}) {
            for (auto* p : {&off, &on})
                p->engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(button), down, 0});
            for (int i = 0; i < (down ? 10 : 30); ++i) block(480, false);
        }
    };
    for (int i = 0; i < 550; ++i) block(480, false);  // the boot: kBootMs 5300 plus a margin
    CHECK(off.engine()->snapshot().booted && on.engine()->snapshot().booted);
    press(ens::ts::Button::TrackEffects);
    for (int k = 0; k < 4; ++k) press(ens::ts::Button::Up);  // VOICE, -FX1-, -FX2-, -DRY-, -AUX-
    for (auto* p : {&off, &on}) {
        const auto row1 = ens::ts::rowText(p->engine()->snapshot().panel, 1);
        std::printf("  track effects: [%s]\n", row1.c_str());
        CHECK(row1.find("-AUX-") != std::string::npos);
    }
    block(480, true);
    for (int i = 0; i < 49; ++i) block(480, false);  // 500 ms from the note
    block(1000, false);
    std::printf("  aux bus: mismatches %d, Main peak %.4f, Aux peak %.4f %.4f\n", mismatches, mainPeak, auxPeak[0],
                auxPeak[1]);
    CHECK(mismatches == 0 && auxPeak[0] > 0.001f && auxPeak[1] > 0.001f);
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
    parameters(dir.getChildFile("params"));
    const auto a = stateRoundTrip(dir.getChildFile("state"));
    stateDuringBoot(dir.getChildFile("boot"), a);
    stateWithoutRoms(dir.getChildFile("noroms"), a);
    stateBadBlobs(dir.getChildFile("bad"));
    projectLeavesSharedFile(dir.getChildFile("shared"));
    stateThreads(dir.getChildFile("threads"));
    standaloneNoRam(dir.getChildFile("standalone"));
    soundFollowsPanel(dir.getChildFile("follow"));
    romsBesideApp(dir.getChildFile("beside"));
    auxBusOff(dir.getChildFile("buses"));  // not "aux": a reserved device name on Windows
    vst3(dir.getChildFile("vst3"));
    dir.deleteRecursively();
    std::printf(failures == 0 ? "app_tests: all passed\n" : "app_tests: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
