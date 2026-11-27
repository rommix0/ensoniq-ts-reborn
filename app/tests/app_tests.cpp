// TS Reborn app checks (from KS-32 Reborn's): checkRomFolder on a temp folder of dummy files the right
// size (never ROM content), the memory file's load and save, the processor's memory-file guards and its
// parameters (A111). On the real ROMs: the sound follow (A111), the plugin state and memory per format
// (A117), the two output buses (A109), the panel editor clicked and painted offscreen (A113-A115), the disk
// drive on temp files (A119), the CD drive (A127, A128; CDR-13 under TSR_DISK_DIR, else synthetic images), the media a
// Project state keeps (A129; ED-001 and CDR-13 under TSR_DISK_DIR, else synthetic <Media>), and the
// built TS Reborn.vst3 loaded, played and its editor opened through JUCE's
// own VST3 host, as a DAW would.
// No check puts a window on the desktop. Every check sets TSR_SETTINGS_DIR to a temp folder.
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>

#include "../source/AppSettings.h"
#include "../source/CdDrive.h"
#include "../source/DiskDrive.h"
#include "../source/MemoryFile.h"
#include "../source/PanelEditor.h"
#include "../source/Processor.h"
#include "../source/RomFolder.h"
#include "app/analog_map.h"
#include "app/panel_layout.h"
#include "ts/panel.h"

namespace {

// Exact float equality is meant where two identical computations are compared; this form does not trip -Wfloat-equal.
inline bool feq(float a, float b) { return !(a < b) && !(a > b); }

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
    CHECK(sound != nullptr && feq(sound->convertFrom0to1(0.0f), 0.0f) && feq(sound->convertFrom0to1(1.0f), 127.0f));
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
    CHECK(feq(*b.params().getRawParameterValue("volume"), 0.5f) &&
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

// Runs a hook when a gesture on the parameter starts. tick() opens its gesture after it has read the
// snapshot and stored the marker, and before it writes `sound`, so the hook stands in for an audio thread
// that runs blocks inside tick()'s writes.
struct GestureHook : juce::AudioProcessorParameter::Listener {
    std::function<void()> onBegin;
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override {
        if (!starting || !onBegin) return;
        const auto f = std::move(onBegin);
        onBegin = nullptr;
        f();
    }
};

// follow (A111, as KS-32 A18) on the real ROMs: panel and MIDI picks are followed into `sound` and never
// sent back, also when the machine moves on while tick() writes; two host changes of `sound` a block
// apart are sent exactly twice and a tick against the stale snapshot does not undo them.
void follow(const juce::File& dir) {
    if (!realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    juce::AudioBuffer<float> buf(4, 480);
    juce::MidiBuffer midi;
    auto* sound = p.params().getParameter("sound");
    const auto block = [&](int n, bool tick = true) {  // n blocks of 10 ms, tick() after each
        for (int i = 0; i < n; ++i) {
            buf.clear();
            p.processBlock(buf, midi);
            midi.clear();
            if (tick) p.tick();
        }
    };
    const auto snap = [&] { return p.engine()->snapshot(); };
    block(kBoot / 10);
    CHECK(snap().program == 0);

    // (i) The panel: Soft2, then Soft3 (U0-0's second and third sounds, programs 1 and 2).
    for (const auto b : {ens::ts::Button::Soft2, ens::ts::Button::Soft3}) {
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), true, 0});
        block(10);
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), false, 0});
        block(30);
    }
    CHECK(snap().programsSent == 0 && soundOf(p) == 2 && snap().program == 2);
    // MIDI in: two program changes one block apart; program 6 is U0-1's first sound.
    midi.addEvent(juce::MidiMessage(0xC0, 5), 0);
    block(1);
    midi.addEvent(juce::MidiMessage(0xC0, 6), 0);
    block(1);
    block(50);
    CHECK(snap().programsSent == 0 && soundOf(p) == 6 && rowStarts(p, 0, "U0-1"));

    // (ii) The machine moves on inside tick()'s writes: C0 07 has landed and tick() follows it, but before
    // `sound` reads 7 the hook runs blocks in which C0 08 lands. The block after takes 7 with its marker
    // and adopts it: nothing is sent, the machine stays on 8, and the next ticks follow 8.
    GestureHook hook;
    sound->addListener(&hook);
    midi.addEvent(juce::MidiMessage(0xC0, 7), 0);
    block(30, false);
    CHECK(snap().program == 7 && soundOf(p) == 6);
    hook.onBegin = [&] {
        midi.addEvent(juce::MidiMessage(0xC0, 8), 0);
        block(30, false);
    };
    p.tick();
    CHECK(soundOf(p) == 7 && snap().program == 8);
    block(1, false);
    std::printf("  follow race: sent %u, program %d\n", snap().programsSent, snap().program);
    CHECK(snap().programsSent == 0 && snap().program == 8);
    block(30);
    CHECK(snap().programsSent == 0 && soundOf(p) == 8 && snap().program == 8);
    sound->removeListener(&hook);

    // (iii) The host: sound 13, then 14 (U0-2's second and third sounds), a block apart.
    const auto host = [&](int n) { sound->setValueNotifyingHost(sound->convertTo0to1(static_cast<float>(n))); };
    host(13);
    block(1, false);  // no tick yet: the snapshot says 13 while the host is about to say 14
    host(14);
    p.tick();  // a stale snapshot (program 13) must not write `sound` back
    CHECK(soundOf(p) == 14);
    block(1);
    CHECK(soundOf(p) == 14);
    block(80);
    std::printf("  follow host: sent %u, sound %d, program %d\n", snap().programsSent, soundOf(p), snap().program);
    CHECK(snap().programsSent == 2 && soundOf(p) == 14 && snap().program == 14 && rowStarts(p, 0, "U0-2"));
}

// A state on R2-2 with TUNE=+02. R2-2 is `B0 00 00 20 02 C0 0E`: program 14 in bank LSB 2. Program 14
// alone is U0-2's third sound, so a restore that drops the bank lands on U0-2 and one that keeps it on
// R2-2. Returns the state for the checks after it (empty when skipped).
juce::MemoryBlock bankState(const juce::File& dir) {
    if (!realRoms(dir)) return {};
    using ens::ts::Button;
    TsProcessor a;
    if (!a.engine()) return {};
    a.prepareToPlay(48000, 480);
    run(a, kBoot);
    press(a, Button::System);
    press(a, Button::Up);
    press(a, Button::Up);
    CHECK(rowStarts(a, 0, "SYSTEM  TUNE=+02"));
    press(a, Button::Sounds);
    press(a, Button::BankSet);
    press(a, Button::BankSet);
    press(a, Button::Bank2);
    press(a, Button::Soft3);
    const auto s = stateOf(a);
    std::printf("  bank state: [%s], select %s\n", ens::ts::rowText(a.engine()->snapshot().panel, 0).c_str(),
                selectOf(s).toRawUTF8());
    CHECK(rowStarts(a, 0, "R2-2") && selectOf(s).equalsIgnoreCase("b000002002c00e"));
    return s;
}

// bank_survives_restarts (A105, A117; as KS-32 int00Restarts): R2-2 comes back from a state, across a
// ROM-folder change and across a new memory file; Erase then gives a cold machine on U0-0 with `sound` 0,
// nothing sent.
void bankRestores(const juce::File& dir, const juce::MemoryBlock& s) {
    if (s.isEmpty() || !realRoms(dir)) return;
    TsProcessor b;
    b.prepareToPlay(48000, 480);
    b.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
    run(b, kRestored);
    CHECK(rowStarts(b, 0, "R2-2"));
    juce::String err;
    CHECK(b.setRomFolder(juce::File(TSR_ROM_DIR), err));  // restart(Keep)
    run(b, kRestored);
    CHECK(rowStarts(b, 0, "R2-2"));
    const auto file = dir.getChildFile("r2.bin");
    CHECK(saveMemoryFile(file, ramOf(s)) && b.setMemoryFile(file, err));  // restart(File)
    run(b, kRestored);
    CHECK(rowStarts(b, 0, "R2-2"));
    if (!b.engine()) return;

    auto* sound = b.params().getParameter("sound");
    sound->setValueNotifyingHost(sound->convertTo0to1(5.0f));
    run(b, 500);
    CHECK(b.engine()->snapshot().program == 5);
    b.eraseMemory();
    CHECK(soundOf(b) == 0);
    run(b, kRestored);
    CHECK(rowStarts(b, 0, "U0-0") && b.engine()->snapshot().programsSent == 0);
}

// data_entry_restored (A106): a project left with Data Entry away from rest (0.9; rest is raw 0x14E) on
// R2-2 comes back as left: after the boot and 500 ms row 0 starts R2-2, the running select is the saved
// one, and System's TUNE is still +02. Data Entry reaching the machine at the boot's end is a slider move
// to the OS, which must not edit the restored play screen.
void dataEntryRestored(const juce::File& dir, const juce::MemoryBlock& r2) {
    if (r2.isEmpty() || !realRoms(dir)) return;
    juce::MemoryBlock s;
    {
        TsProcessor a;  // r2 with dataEntry 0.9: a state taken before the boot carries r2's RAM and select
        a.setStateInformation(r2.getData(), static_cast<int>(r2.getSize()));
        a.params().getParameter("dataEntry")->setValueNotifyingHost(0.9f);
        s = stateOf(a);
    }
    CHECK(selectOf(s).equalsIgnoreCase(selectOf(r2)) && ramOf(s) == ramOf(r2));
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    p.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
    CHECK(feq(p.params().getParameter("dataEntry")->getValue(), 0.9f));
    run(p, kRestored + 500);
    std::printf("  data entry restore: [%s], select %s\n", ens::ts::rowText(p.engine()->snapshot().panel, 0).c_str(),
                selectOf(stateOf(p)).toRawUTF8());
    CHECK(rowStarts(p, 0, "R2-2") && selectOf(stateOf(p)).equalsIgnoreCase(selectOf(r2)));
    using ens::ts::Analog;
    CHECK(p.engine()->machine().analog(Analog::DataEntry) == ens::app::analogRaw(Analog::DataEntry, 0.9f));
    press(p, ens::ts::Button::System);
    CHECK(rowStarts(p, 0, "SYSTEM  TUNE=+02"));
    // The input is live: the same slider moved on the TUNE page edits it, so the checks above can fail.
    p.params().getParameter("dataEntry")->setValueNotifyingHost(0.1f);
    run(p, 300);
    std::printf("  data entry on TUNE: [%s]\n", ens::ts::rowText(p.engine()->snapshot().panel, 0).c_str());
    CHECK(rowStarts(p, 0, "SYSTEM  TUNE=") && !rowStarts(p, 0, "SYSTEM  TUNE=+02"));
}

// zero_length_block / bypassed_block (A109): a 0-sample block with Aux off still takes its messages (the
// VST3 wrapper makes such blocks when the host passes outputs), and so does a host-bypassed block, whose
// output is silent. Either way a note-off is not lost and no note stays held.
void zeroAndBypassedBlocks(const juce::File& dir) {
    if (!realRoms(dir)) return;
    TsProcessor p;
    CHECK(p.disableNonMainBuses() && p.getTotalNumOutputChannels() == 2);
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    const auto held = [&] { return (p.engine()->snapshot().notes[0] >> 60) & 1; };
    const auto msg = [](uint8_t a, uint8_t b, uint8_t c) {
        juce::MidiBuffer m;
        const uint8_t d[] = {a, b, c};
        m.addEvent(d, 3, 0);
        return m;
    };
    juce::AudioBuffer<float> buf(2, 480);
    juce::MidiBuffer none;
    for (int i = 0; i < kBoot / 10; ++i) p.processBlock(buf, none);
    auto on = msg(0x90, 0x3C, 0x64);
    p.processBlock(buf, on);
    CHECK(held() == 1);
    juce::AudioBuffer<float> empty(2, 0);
    auto off = msg(0x80, 0x3C, 0x00);
    p.processBlock(empty, off);
    CHECK(held() == 0);

    on = msg(0x90, 0x3C, 0x64);
    p.processBlock(buf, on);
    for (int i = 0; i < 20; ++i) p.processBlock(buf, none);  // 200 ms: the note sounds
    CHECK(held() == 1 && buf.getMagnitude(0, 480) > 0.001f);
    off = msg(0x80, 0x3C, 0x00);
    p.processBlockBypassed(buf, off);
    CHECK(held() == 0 && feq(buf.getMagnitude(0, 480), 0.0f));
    std::printf("  zero-length and bypassed blocks: note 60 held %d\n", static_cast<int>(held()));
}

// standalone_ignores_state (A121): a File processor given a Project state (R2-2, TUNE=+02) before its first
// block ignores it: no sound to replay, so it boots on U0-0 with `sound` 0, its memory still the file's
// (none here: cold, so TUNE=+00).
void standaloneStateBeforeBoot(const juce::File& dir, const juce::MemoryBlock& s) {
    if (s.isEmpty() || !realRoms(dir)) return;
    TsProcessor p(MemoryMode::File);
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    p.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
    run(p, kRestored);
    CHECK(rowStarts(p, 0, "U0-0") && p.engine()->snapshot().programsSent == 0);
    CHECK(p.getParameters()[0] != nullptr);
    for (auto* prm : p.getParameters()) CHECK(std::abs(prm->getValue() - prm->getDefaultValue()) < 1e-6f);
    press(p, ens::ts::Button::System);
    CHECK(rowStarts(p, 0, "SYSTEM  TUNE=+00"));
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

// rom_folder_in_use (A125): a folder used but not stored (a VST3 package's own ROMs) is still the folder the
// settings page shows and the memory-file guard checks: a memory file inside it is refused.
void romFolderInUse(const juce::File& dir) {
    setSettingsDir(dir);
    const auto packaged = dir.getChildFile("Contents/Resources/roms");
    dummyRoms(packaged);
    TsProcessor p;
    juce::String err;
    p.setRomFolder(packaged, err, false);
    CHECK(p.romFolder() == packaged && !p.romFolderStored() && p.settings().romFolder() == juce::File());
    CHECK(!p.setMemoryFile(packaged.getChildFile("os/ts10_310l.bin"), err) && err.isNotEmpty());
    CHECK(p.settings().memoryFile() != packaged.getChildFile("os/ts10_310l.bin"));
    const auto chosen = dir.getChildFile("chosen");
    dummyRoms(chosen);
    p.setRomFolder(chosen, err);
    CHECK(p.romFolder() == chosen && p.romFolderStored() && p.settings().romFolder() == chosen);
    // The stored folder taken again without storing (the start-up path for a bad stored folder) is still the
    // stored one: the page must not call it the package's.
    p.setRomFolder(chosen, err, false);
    CHECK(p.romFolderStored());
}

// rom_folder_order (A125): the stored folder if it holds the six ROMs, else "roms" beside the program
// (stored), else Contents/Resources/roms when the program sits in a VST3 package (not stored), else the
// stored folder as it is (its problem is shown), not stored.
void romFolderOrder(const juce::File& dir) {
    const auto is = [](const RomFolderChoice& c, const juce::File& want, bool store) {
        return c.dir == want && c.store == store;
    };
    // stored good
    const auto a = dir.getChildFile("a"), b = dir.getChildFile("b/roms");
    dummyRoms(a);
    dummyRoms(b);
    CHECK(is(findRomFolder(a, dir.getChildFile("b/TS Reborn.exe")), a, false));
    // stored missing: the folder beside the program, stored
    const auto app = dir.getChildFile("app");
    dummyRoms(app.getChildFile("roms"));
    CHECK(is(findRomFolder(dir.getChildFile("gone"), app.getChildFile("TS Reborn.exe")), app.getChildFile("roms"),
             true));
    // package: Contents/Resources/roms, not stored
    const auto contents = dir.getChildFile("pkg/TS Reborn.vst3/Contents");
    contents.getChildFile("x86_64-win").createDirectory();
    dummyRoms(contents.getChildFile("Resources/roms"));
    const auto plugin = contents.getChildFile("x86_64-win/TS Reborn.vst3");
    CHECK(is(findRomFolder({}, plugin), contents.getChildFile("Resources/roms"), false));
    // not a package: a ../Resources/roms is never taken
    const auto plain = dir.getChildFile("plain");
    plain.getChildFile("bin").createDirectory();
    dummyRoms(plain.getChildFile("Resources/roms"));
    CHECK(is(findRomFolder({}, plain.getChildFile("bin/TS Reborn.exe")), juce::File(), false));
    // only a bad stored folder: kept, so its problem is reported
    const auto bad = dir.getChildFile("bad");
    write(bad.getChildFile("os/ts10_310h.bin"), 0x40000);
    CHECK(is(findRomFolder(bad, plain.getChildFile("bin/TS Reborn.exe")), bad, false));
    // beside beats the package
    dummyRoms(contents.getChildFile("x86_64-win/roms"));
    CHECK(is(findRomFolder({}, plugin), contents.getChildFile("x86_64-win/roms"), true));
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
                mismatches += !feq(a.getSample(c, i), b.getSample(c, i) + b.getSample(c + 2, i));
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

// The panel editor, headless: never put on the desktop, driven by synthetic mouse events, painted into an
// image. TSR_SNAPSHOT_DIR=<folder> also writes the images there (panel.png, panel-codes.png, panel-noroms.png).
constexpr float kEdScale = 1546.0f / ens::app::kDesignW;  // the default size (A112)

const ens::app::Control& control(ens::app::ControlKind kind, int id) {
    for (const auto& c : ens::app::panelLayout())
        if (c.kind == kind && c.id == id) return c;
    std::abort();
}

// The board's part drawn for a table control: the child at its design rect's centre.
juce::Component& part(juce::Component& ed, const ens::app::Control& c) {
    auto& board = *ed.getChildComponent(0);
    auto* p = board.getComponentAt(c.r.x + c.r.w / 2, c.r.y + c.r.h / 2);
    if (p == nullptr || p == &board) std::abort();
    return *p;
}

juce::MouseEvent mouse(juce::Component& c, juce::Point<float> at, juce::Point<float> down) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0, 0, 0, 0, &c, &c, now, down, now, 1,
            false};
}

void click(TsProcessor& p, juce::Component& ed, ens::ts::Button b) {  // held 100 ms, then 300 ms
    auto& c = part(ed, control(ens::app::ControlKind::Button, static_cast<int>(b)));
    const juce::Point<float> at(5, 5);
    c.mouseDown(mouse(c, at, at));
    run(p, 100);
    c.mouseUp(mouse(c, at, at));
    run(p, 300);
}

// Pixels in a design rect whose channel `ch` (0 red, 1 green) is above `min`.
int brightPixels(const juce::Image& img, ens::app::Rect r, int ch, int min) {
    int n = 0;
    for (int y = static_cast<int>(r.y * kEdScale); y < static_cast<int>((r.y + r.h) * kEdScale); ++y)
        for (int x = static_cast<int>(r.x * kEdScale); x < static_cast<int>((r.x + r.w) * kEdScale); ++x) {
            const auto px = img.getPixelAt(x, y);
            n += (ch == 0 ? px.getRed() : px.getGreen()) > min;
        }
    return n;
}

juce::Image paintEditor(juce::Component& ed, const char* name) {
    auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
    const auto dir = juce::SystemStats::getEnvironmentVariable("TSR_SNAPSHOT_DIR", {});
    if (dir.isNotEmpty()) {
        const auto f = juce::File(dir).getChildFile(name);
        f.deleteFile();
        juce::FileOutputStream out(f);
        if (out.openedOk()) juce::PNGImageFormat().writeImageToStream(img, out);
    }
    return img;
}

// Main's peak while `note` is held 300 ms from a press at design depth y (0 = the key's back edge), then
// released for 700 ms.
float keyPeak(TsProcessor& p, juce::Component& ed, int note, float y) {
    auto& c = part(ed, control(ens::app::ControlKind::Key, note));
    const juce::Point<float> at(8, y);
    c.mouseDown(mouse(c, at, at));
    juce::AudioBuffer<float> buf(4, 480);
    juce::MidiBuffer none;
    float peak = 0;
    for (int i = 0; i < 100; ++i) {
        if (i == 30) c.mouseUp(mouse(c, at, at));
        buf.clear();
        p.processBlock(buf, none);
        if (i < 30) peak = std::max(peak, buf.getMagnitude(0, 0, 480));
    }
    return peak;
}

void editor(const juce::File& dir) {
    using ens::app::ControlKind;
    using ens::ts::Button;
    // No ROMs: the display gives the reason.
    setSettingsDir(dir.getChildFile("none"));
    AppSettings().setRomFolder(dir.getChildFile("empty"));
    {
        TsProcessor p;
        CHECK(p.engine() == nullptr && p.romError().isNotEmpty());
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        const auto img = paintEditor(*ed, "panel-noroms.png");
        CHECK(brightPixels(img, ens::app::kDisplayText, 1, 120) > 100);
    }

    if (!realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, kBoot);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    CHECK(ed->getWidth() == 1546 && ed->getHeight() == 552 && !ed->isOnDesktop());

    // Presets, Bank 3, the lower-left soft button: a preset, with LEDs 1 (Presets) and 10 (Bank 3) lit.
    click(p, *ed, Button::Presets);
    click(p, *ed, Button::Bank3);
    click(p, *ed, Button::Soft4);
    std::printf("  editor row 1: %s\n", ens::ts::rowText(p.engine()->snapshot().panel, 1).c_str());
    CHECK(rowStarts(p, 1, "PSET"));
    const auto leds = p.engine()->snapshot().panel.leds;
    CHECK((leds >> 1 & 1u) && (leds >> 10 & 1u));

    // The editor's timer takes the snapshot (30 Hz; no message loop runs here, so it is called when due); the
    // image shows the text and those LEDs.
    auto& pe = static_cast<PanelEditor&>(*ed);
    for (int i = 0; i < 100 && pe.snap().panel.leds != leds; ++i) {
        juce::Thread::sleep(10);
        juce::Timer::callPendingTimersSynchronously();
    }
    const auto img = paintEditor(*ed, "panel.png");
    const auto ledArea = [](int led) { return control(ControlKind::Led, led).r; };
    CHECK(brightPixels(img, ens::app::kDisplayText, 1, 120) > 100);
    CHECK(brightPixels(img, ledArea(1), 0, 200) > 0 && brightPixels(img, ledArea(10), 0, 200) > 0);
    CHECK(brightPixels(img, ledArea(9), 0, 200) == 0);  // Sounds: off
    // The decals (A122): the TS-10 logo's lavender triangle (blue over green, as the photo) and the badge.
    const auto pixels = [&img](ens::app::Rect r, auto want) {
        int n = 0;
        for (int y = static_cast<int>(r.y * kEdScale); y < static_cast<int>((r.y + r.h) * kEdScale); ++y)
            for (int x = static_cast<int>(r.x * kEdScale); x < static_cast<int>((r.x + r.w) * kEdScale); ++x)
                n += want(img.getPixelAt(x, y));
        return n;
    };
    const auto decal = [](const char* label) {
        for (const auto& c : ens::app::panelLayout())
            if (c.ink == ens::app::Ink::Decal && std::strcmp(c.label, label) == 0) return c.r;
        std::abort();
    };
    const int triangle = pixels(decal("TS-10"), [](juce::Colour c) { return c.getBlue() > c.getGreen() + 15; });
    const int badge = pixels(decal("ensoniq"), [](juce::Colour c) { return c.getRed() > 120; });
    std::printf("  decals: %d triangle pixels, %d badge pixels\n", triangle, badge);
    CHECK(triangle > 50 && badge > 100);
    // "Show display codes": every cell as two hex digits, a different picture of the same cells.
    pe.toggleHexCodes();
    const int codes = brightPixels(paintEditor(*ed, "panel-codes.png"), ens::app::kDisplayText, 1, 120);
    CHECK(codes > 100 && codes != brightPixels(img, ens::app::kDisplayText, 1, 120));
    pe.toggleHexCodes();

    // The pitch wheel: a drag up raises it inside one gesture, the release springs it back (A111).
    auto* bend = p.params().getParameter("pitchWheel");
    auto& wheel = part(*ed, control(ControlKind::Wheel, static_cast<int>(ens::ts::Analog::PitchWheel)));
    const juce::Point<float> from(20, 100), to(20, 50);
    wheel.mouseDown(mouse(wheel, from, from));
    wheel.mouseDrag(mouse(wheel, to, from));
    CHECK(bend->getValue() > 0.7f);
    wheel.mouseUp(mouse(wheel, to, from));
    CHECK(feq(bend->getValue(), 0.5f));

    // A115: the front of a key is louder than its back.
    const float back = keyPeak(p, *ed, 60, 5), front = keyPeak(p, *ed, 60, 340);
    std::printf("  key peak back %.4f, front %.4f\n", back, front);
    CHECK(front > 2 * back && back > 0);
    ed.reset();
}

// One Write Data of cylinder 0, head 0, sector 0 put straight into the floppy controller between blocks, as
// fdc_write_data_counts_write does: MSR at 0x280001, data at 0x280003 (CPU A1 = FDC A0). Waits, a block at a
// time, for the OS to leave the controller idle (MSR 0x80). Returns ST1: 0x80 written, 0x02 write protected,
// 0x01 no address mark (the wrong data rate); -1 when the controller never went idle.
int writeSector(TsProcessor& p, uint8_t fill = 0xA5) {
    auto& m = p.engine()->machine();
    for (int i = 0; i < 100 && m.read8(0x280001) != 0x80; ++i) run(p, 10);
    if (m.read8(0x280001) != 0x80) {
        std::printf("  write data: MSR %02x\n", m.read8(0x280001));
        return -1;
    }
    m.write8(0x280001, 0x88);  // the DD data rate, as the OS selects it from DENS before a transfer (T24)
    for (const int b : {0x45, 0x00, 0, 0, 0, 2, 0, 0x1B, 0xFF}) m.write8(0x280003, static_cast<uint8_t>(b));
    for (int i = 0; i < 512 && (m.read8(0x280001) & 0x20) != 0; ++i) m.write8(0x280003, fill);
    uint8_t r[7];
    for (auto& v : r) v = m.read8(0x280003);
    std::printf("  write data: ST0-2 %02x %02x %02x, MSR %02x\n", r[0], r[1], r[2], m.read8(0x280001));
    return r[1];
}

// disk_drive (A119): open, the size check, a write by the controller, save, write protect, a read-only file,
// a new blank disk saved as, a restart that keeps the disk, and the slot drawn on the panel. Each save is
// asynchronous: the copy comes with a block, the write with the tick after it. Temp files only.
void diskDrive(const juce::File& dir) {
    if (!realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, kBoot);
    auto& d = p.drive();
    const auto step = [&] {  // one block, then the tick
        run(p, 10);
        p.tick();
    };
    CHECK(d.slotText().isEmpty() && !d.modified() && !p.engine()->snapshot().diskIn);

    juce::String err;
    const auto img = dir.getChildFile("disk.img");
    write(img, 819200);  // raw DD, zeros
    CHECK(d.open(img, err));
    step();
    CHECK(d.slotText() == "disk.img" && p.engine()->snapshot().diskIn && !d.modified());

    // The wrong size: refused, naming the sizes; the disk in stays.
    const auto bad = dir.getChildFile("bad.img");
    write(bad, 1000);
    CHECK(!d.open(bad, err) && err.contains("819200") && err.contains("1638400"));
    step();
    CHECK(d.slotText() == "disk.img" && p.engine()->snapshot().diskIn);

    // The controller writes: "(modified)"; Save writes the file through <file>.tmp and clears it.
    CHECK(writeSector(p) == 0x80);
    step();
    std::printf("  disk drive: [%s]\n", d.slotText().toRawUTF8());
    CHECK(d.modified() && d.slotText().endsWith(" (modified)"));
    CHECK(d.save(err));
    step();
    const auto saved = fileBytes(img);
    CHECK(!d.modified() && d.slotText() == "disk.img" && !img.getSiblingFile("disk.img.tmp").exists());
    CHECK(saved.getSize() == 819200 && static_cast<uint8_t>(saved[0]) == 0xA5 && static_cast<uint8_t>(saved[511]) == 0xA5);

    // Write Protected: the controller refuses (ST1 NW), nothing changes; off again, it writes.
    d.setWriteProtected(true);
    step();
    CHECK(d.writeProtected() && writeSector(p) == 0x02);
    step();
    CHECK(!d.modified());
    d.setWriteProtected(false);
    step();
    CHECK(writeSector(p) == 0x80);
    step();
    CHECK(d.modified());

    // A read-only file: Save is refused and the file is unchanged.
    CHECK(img.setReadOnly(true));
    CHECK(!d.save(err) && err.isNotEmpty());
    step();
    CHECK(fileBytes(img) == saved && d.modified());
    CHECK(img.setReadOnly(false));

    // A new HD disk saved as x.img: 1 638 400 bytes, the drive's file from then on.
    d.newBlank(ens::ts::Density::High);
    step();
    CHECK(d.slotText() == "Untitled (blank)" && !d.modified());
    CHECK(!d.save(err) && err.isNotEmpty());  // no file yet: Save As only
    const auto x = dir.getChildFile("x.img");
    CHECK(d.saveAs(x, err));
    step();
    CHECK(x.getSize() == 1638400 && !d.modified() && d.slotText() == "x.img");

    // A restart (the same ROM folder) keeps the disk in the drive.
    const auto* engine = p.engine();
    CHECK(p.setRomFolder(juce::File(TSR_ROM_DIR), err) && p.engine() != engine);
    step();
    CHECK(p.engine()->snapshot().diskIn && d.slotText() == "x.img" && !d.modified());

    // A save asked for just before a restart: the new engine is asked for the copy, and it is written.
    CHECK(d.open(img, err));
    step();
    CHECK(writeSector(p, 0x5A) == 0x80);
    step();
    CHECK(d.modified() && d.save(err));
    CHECK(p.setRomFolder(juce::File(TSR_ROM_DIR), err));
    step();  // the drive sees the new engine at this tick and asks it for the copy
    step();
    const auto across = fileBytes(img);
    CHECK(!d.modified() && across.getSize() == 819200 && static_cast<uint8_t>(across[0]) == 0x5A);
    CHECK(!img.getSiblingFile("disk.img.tmp").exists());

    // A save followed by New before its copy came: it fails, and the old file is kept byte for byte.
    int calls = 0;
    bool savedOk = true;
    CHECK(d.save(err, [&](bool ok, const juce::String&) {
        ++calls;
        savedOk = ok;
    }));
    CHECK(d.newBlank(ens::ts::Density::Double));
    step();
    step();
    CHECK(calls == 1 && !savedOk && fileBytes(img) == across && !img.getSiblingFile("disk.img.tmp").exists());
    CHECK(d.slotText() == "Untitled (blank)");

    // A save in flight: an action asked meanwhile waits for it, asks nothing, and runs after the write; a save
    // that failed drops it (A120).
    CHECK(d.open(img, err));
    step();
    CHECK(writeSector(p, 0x33) == 0x80);
    step();
    CHECK(d.modified() && d.save(err));
    bool ran = false;
    d.askThen([&] { ran = true; });  // would pop a dialog before the fix; now chained
    CHECK(!ran);
    step();
    step();
    CHECK(ran && !d.modified() && static_cast<uint8_t>(fileBytes(img)[0]) == 0x33);

    // A save stuck because the audio stopped: one action waits for it, a second one asks again (so Don't Save
    // still quits or goes on), and with the device stopped none waits; the chained one runs once audio is back.
    CHECK(writeSector(p, 0x44) == 0x80);
    step();
    CHECK(d.modified() && d.save(err) && d.waitsForSave());
    ran = false;
    d.askThen([&] { ran = true; });
    CHECK(!ran && !d.waitsForSave());  // a second askThen would show the prompt, not chain forever
    p.releaseResources();
    CHECK(!p.prepared());
    p.prepareToPlay(48000, 480);
    step();
    step();
    CHECK(ran && !d.modified() && static_cast<uint8_t>(fileBytes(img)[0]) == 0x44);
    CHECK(writeSector(p, 0x45) == 0x80);
    step();
    CHECK(d.save(err));
    p.releaseResources();
    CHECK(d.modified() && !d.waitsForSave());  // device stopped: the save cannot finish, so askThen prompts
    p.prepareToPlay(48000, 480);
    step();
    step();
    CHECK(!d.modified() && static_cast<uint8_t>(fileBytes(img)[0]) == 0x45);

    // No audio running: Open is refused at once, saying so, not "busy" (A119).
    {
        TsProcessor q;
        if (q.engine()) {
            juce::String e2;
            CHECK(!q.drive().open(img, e2) && e2.contains("no audio") && !q.drive().newBlank(ens::ts::Density::Double));
            CHECK(!q.drive().open(img, e2) && e2.contains("no audio"));
        }
    }

    // The slot on the panel shows the text; empty after Eject.
    auto slot = control(ens::app::ControlKind::Decor, -1).r;
    slot = {slot.x + 26, slot.y + 22, slot.w - 52, slot.h - 44};
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    // A click on the drive reaches the drive's control (its mouseDown opens the menu), not the board under it.
    auto* tip = dynamic_cast<juce::SettableTooltipClient*>(&part(*ed, control(ens::app::ControlKind::Decor, -1)));
    CHECK(tip != nullptr && tip->getTooltip().contains("disk menu"));
    const int withDisk = brightPixels(paintEditor(*ed, "panel-disk.png"), slot, 0, 120);
    d.eject();
    step();
    const int without = brightPixels(paintEditor(*ed, "panel-nodisk.png"), slot, 0, 120);
    std::printf("  disk slot: %d bright pixels with the disk, %d without\n", withDisk, without);
    CHECK(withDisk > 50 && without == 0 && d.slotText().isEmpty() && !p.engine()->snapshot().diskIn);
    ed.reset();
}

// The CD drive (A127, A128). CDR-13's .cue under TSR_DISK_DIR (the user's images, read only); an empty File and
// a skip line without it.
juce::File cdr13() {
    const juce::String dir = TSR_DISK_DIR;
    if (dir.isNotEmpty()) {
        const auto f = juce::File(dir).getChildFile(
            "CD-ROMs/Ensoniq.CDR-13.InVision.Percussion.ENSONiQ-DELiRiUM/dlm-cdr13.cue");
        if (f.existsAsFile()) return f;
    }
    std::printf("  skip: disk images not found\n");
    return {};
}

// As run(), but after each block it gives the reader thread up to a second to answer the sector the block asked
// for, then ticks: the machine sees the CD at about the pace it would in real time, whatever the speed here.
void runCd(TsProcessor& p, int ms) {
    juce::AudioBuffer<float> buf(4, 480);
    juce::MidiBuffer none;
    for (int i = 0; i < ms / 10; ++i) {
        buf.clear();
        p.processBlock(buf, none);
        uint32_t medium = 0, lba = 0;
        auto* port = p.engine()->cdPort();
        for (int t = 0; port != nullptr && t < 1000 && port->pending(medium, lba); ++t) juce::Thread::sleep(1);
        p.tick();
    }
}

void pressCd(TsProcessor& p, ens::ts::Button b) {  // held 100 ms, then 300 ms for the display
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), true, 0});
    runCd(p, 100);
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), false, 0});
    runCd(p, 300);
}

bool shows(TsProcessor& p, const char* text) {
    const auto& panel = p.engine()->snapshot().panel;
    return ens::ts::rowText(panel, 0).find(text) != std::string::npos ||
           ens::ts::rowText(panel, 1).find(text) != std::string::npos;
}

void printRows(TsProcessor& p) {
    const auto& panel = p.engine()->snapshot().panel;
    std::printf("  |%s|\n  |%s|\n", ens::ts::rowText(panel, 0).c_str(), ens::ts::rowText(panel, 1).c_str());
}

// The SCSI STATUS page at DEVICE-ID 4, as ts_cd_status_shows_volume opens it (tests/ts_disks.h): Storage, Soft4,
// Up four times, then 1.5 s for the OS's poll to mount the volume.
void openScsiStatus(TsProcessor& p) {
    pressCd(p, ens::ts::Button::Storage);
    pressCd(p, ens::ts::Button::Soft4);
    for (int i = 0; i < 4; ++i) pressCd(p, ens::ts::Button::Up);
    runCd(p, 1500);
}

// cd_drive (A127, A128): insert CDR-13's .cue, the refusals, a restart that keeps the CD, eject, and new medium ids.
void cdDrive(const juce::File& dir) {
    const auto cue = cdr13();
    if (cue == juce::File() || !realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, 100);
    auto& cd = p.cdDrive();
    CHECK(cd.name().isEmpty() && p.engine()->snapshot().cdMedium == 0);

    juce::String err;
    CHECK(cd.insert(cue, err));
    runCd(p, 10);
    const uint32_t medium = p.engine()->snapshot().cdMedium;
    CHECK(cd.name() == "dlm-cdr13.cue" && medium != 0);

    // An .iso that is not whole 2048-byte sectors, and a .cue of another layout: refused, the CD in stays.
    const auto iso = dir.getChildFile("short.iso");
    write(iso, 1000);
    CHECK(!cd.insert(iso, err) && err.contains("2048"));
    const auto mode2 = dir.getChildFile("mode2.cue");
    CHECK(mode2.replaceWithText("FILE \"x.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n"));
    CHECK(!cd.insert(mode2, err) && err.contains("MODE1/2352"));
    CHECK(!cd.insert(dir.getChildFile("x.wav"), err) && err.isNotEmpty());
    runCd(p, 10);
    CHECK(cd.name() == "dlm-cdr13.cue" && p.engine()->snapshot().cdMedium == medium);

    // A restart (the same ROM folder) keeps the CD in, under the same medium id.
    const auto* engine = p.engine();
    CHECK(p.setRomFolder(juce::File(TSR_ROM_DIR), err) && p.engine() != engine);
    runCd(p, 10);
    CHECK(cd.name() == "dlm-cdr13.cue" && p.engine()->snapshot().cdMedium == medium);

    CHECK(cd.eject());
    runCd(p, 10);
    CHECK(cd.name().isEmpty() && p.engine()->snapshot().cdMedium == 0);

    // The same file again is a new medium: ids are never reused.
    CHECK(cd.insert(cue, err));
    runCd(p, 10);
    CHECK(p.engine()->snapshot().cdMedium > medium);

    // No audio running: refused at once, saying so.
    TsProcessor q;
    if (q.engine()) CHECK(!q.cdDrive().insert(cue, err) && err.contains("no audio"));
}

// cd_drive_loads: CDR-13 in the drive from power-on; the SCSI STATUS page shows its volume, read through the reader
// thread. In Project mode the sectors served are in the record (A131).
void cdDriveLoads(const juce::File& dir) {
    const auto cue = cdr13();
    if (cue == juce::File() || !realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    juce::String err;
    CHECK(p.cdDrive().insert(cue, err));
    runCd(p, kBoot);
    openScsiStatus(p);
    printRows(p);
    CHECK(shows(p, "DEVICE-ID=4") && shows(p, "STATUS=CDR-013"));
    bool recorded = false;
    for (const auto& m : p.record().media())
        recorded |= m.kind == ens::app::SavedMedium::Kind::Cd && m.label == "CDR-013" && m.blocks.count(0) != 0;
    CHECK(recorded);
    if (!recorded) return;

    // serveFrom (a stand-in, Task 9): asks for its medium id are answered from the recorded sectors only. Asked here
    // between blocks, as the CD target would (the test thread is the audio thread), with no read in progress.
    const auto& saved = p.record().media().back();
    const uint32_t id = p.cdDrive().newMedium();
    p.cdDrive().serveFrom(&saved, id);
    auto* port = p.engine()->cdPort();
    const auto answer = [&](uint32_t lba, bool& ok) {
        port->ask(id, lba);
        for (int t = 0; t < 1000; ++t) {
            if (port->answered(id, lba, ok)) return true;
            juce::Thread::sleep(1);
        }
        return false;
    };
    bool ok = false;
    CHECK(answer(0, ok) && ok && std::memcmp(port->data(), saved.blocks.at(0).data(), 2048) == 0);
    CHECK(saved.blocks.count(5) == 0 && answer(5, ok) && !ok);  // a sector not recorded fails
    p.cdDrive().serveFrom(nullptr, id);
    CHECK(answer(0, ok) && !ok);  // no longer served
}

// cd_drive_missing_file (Review Focus 1): a small synthetic .iso, mounted, then deleted before *LOAD* reads it: the
// reader answers failure, the machine keeps running and the display shows the OS's error, not the Load page. In
// File mode (the Standalone) nothing is recorded (A121).
void cdDriveMissingFile(const juce::File& dir) {
    if (!realRoms(dir)) return;
    // 64 zero sectors with an Ensoniq Device ID block (512-byte block 1, in sector 0): 0xFF at 30, the label at
    // 31-37, "ID" at 38-39 (A131).
    std::vector<char> img(64 * 2048);
    char* id = img.data() + 512;
    id[30] = static_cast<char>(0xFF);
    std::memcpy(id + 31, "TEST-CD", 7);
    id[38] = 'I';
    id[39] = 'D';
    const auto iso = dir.getChildFile("test.iso");
    CHECK(dir.createDirectory() && iso.replaceWithData(img.data(), img.size()));
    TsProcessor p(MemoryMode::File);
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    juce::String err;
    CHECK(p.cdDrive().insert(iso, err));
    runCd(p, kBoot);
    openScsiStatus(p);
    printRows(p);
    CHECK(shows(p, "DEVICE-ID=4") && shows(p, "STATUS=TEST-CD"));
    CHECK(iso.deleteFile());
    const auto frames = p.engine()->snapshot().frames;
    pressCd(p, ens::ts::Button::Soft4);  // *LOAD*
    runCd(p, 3000);
    printRows(p);
    CHECK(p.engine()->snapshot().frames > frames + 90000);  // 3.4 s of 29 762 frames a second, nearly all
    CHECK(!shows(p, "TYPE=") && !shows(p, "LOAD FILE="));
    // As observed: "DISK DRIVE NOT RESPONDING / PRESS ANY BUTTON TO CONTINUE..."; a button takes the OS on.
    CHECK(shows(p, "NOT RESPONDING"));
    pressCd(p, ens::ts::Button::Up);
    printRows(p);
    CHECK(!shows(p, "NOT RESPONDING") && shows(p, "STATUS=INVALID"));
    CHECK(p.record().media().empty());
}

// drop_goes_to_cd: a .cue dropped on the panel goes in the CD drive (here a synthetic one whose FILE names another
// folder in capitals: found beside the .cue without case, A127), an .img in the floppy drive.
void dropGoesToCd(const juce::File& dir) {
    if (!realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, 10);
    const auto cue = dir.getChildFile("data.cue");
    CHECK(cue.replaceWithText("FILE \"E:\\RIPS\\DATA.BIN\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"));
    write(dir.getChildFile("data.bin"), 4 * 2352);
    const auto img = dir.getChildFile("d.img");
    write(img, 819200);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* drop = dynamic_cast<juce::FileDragAndDropTarget*>(ed.get());
    CHECK(drop != nullptr);
    if (drop == nullptr) return;
    drop->filesDropped(juce::StringArray(cue.getFullPathName()), 0, 0);
    run(p, 10);
    CHECK(p.cdDrive().name() == "data.cue" && p.engine()->snapshot().cdMedium != 0 && !p.engine()->snapshot().diskIn);
    drop->filesDropped(juce::StringArray(img.getFullPathName()), 0, 0);
    run(p, 10);
    CHECK(p.drive().slotText() == "d.img" && p.engine()->snapshot().diskIn && p.cdDrive().name() == "data.cue");
    ed.reset();
}

// ---- Project states keep the sampled sounds (A129) ----

// ED-001 under TSR_DISK_DIR (tests/ts_disks.h's kEd001); an empty File and a skip line without it.
juce::File ed001() {
    const juce::String dir = TSR_DISK_DIR;
    if (dir.isNotEmpty()) {
        const auto f = juce::File(dir).getChildFile(
            "EPS & EPS-16/Factory Sounds/Ensoniq EPS16 Factory Floppy Archive (15 disks, fixed) [IMG & GKH]/"
            "Ensoniq EPS16 Factory Floppy Archive (15 disks) [IMG & GKH]/GKH/ED-001.GKH");
        if (f.existsAsFile()) return f;
    }
    std::printf("  skip: disk images not found\n");
    return {};
}

bool until(TsProcessor& p, const char* text, int ms) {
    for (int t = 0; t < ms && !shows(p, text); t += 10) runCd(p, 10);
    return shows(p, text);
}

void hold(TsProcessor& p, ens::ts::Button b) {  // a bank button under BANKSET=8: 60 ms, as tests/ts_disks.h
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), true, 0});
    runCd(p, 60);
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), false, 0});
}

// The disk's Load page (Storage twice, as openDiskLoad in tests/ts_disks.h) on its first sound `name`, then *YES* and
// `bank` load it into S8. True once back on the Load page.
bool loadFromFloppy(TsProcessor& p, const char* name, ens::ts::Button bank) {
    using ens::ts::Button;
    pressCd(p, Button::Storage);
    pressCd(p, Button::Storage);
    const auto file = juce::String("FILE=") + name;
    if (!until(p, file.toRawUTF8(), 3000)) return false;
    printRows(p);
    pressCd(p, Button::Soft3);
    if (!shows(p, "BANKSET=8")) return false;
    hold(p, bank);
    const auto loading = juce::String("LOADING ") + name;
    if (!until(p, loading.toRawUTF8(), 2000)) return false;
    const bool done = until(p, "LOAD FILE=", 30000);
    printRows(p);
    return done;
}

// From the SCSI STATUS page with the volume mounted (loadFromCd in tests/test_app_media.cpp): *LOAD*, then down `path`
// (Soft1, Up to the entry, *YES*), and `bank` loads the sound into S8.
bool loadFromCd(TsProcessor& p, const std::vector<const char*>& path, ens::ts::Button bank) {
    using ens::ts::Button;
    pressCd(p, Button::Soft4);
    if (!until(p, "TYPE=", 5000)) return false;
    for (const char* name : path) {
        auto field = juce::String("FILE=") + name;
        field = field.paddedRight(' ', 17);  // names are 12 characters, space padded
        runCd(p, 500);
        pressCd(p, Button::Soft1);
        for (int i = 0; i < 40 && !shows(p, field.toRawUTF8()); ++i) pressCd(p, Button::Up);
        if (!shows(p, field.toRawUTF8())) return false;
        pressCd(p, Button::Soft3);
    }
    if (!shows(p, "BANKSET=8")) return false;
    hold(p, bank);
    const auto loading = juce::String("LOADING ") + path.back();
    if (!until(p, loading.toRawUTF8(), 2000)) return false;
    const bool done = until(p, "LOAD FILE=", 60000);
    printRows(p);
    return done;
}

// Runs until the restore (A131) and the boot are over, at most 130 s: the ms it took, -1 when it did not end.
int runRestore(TsProcessor& p) {
    bool was = false;
    for (int ms = 0; ms < 130000; ms += 10) {
        runCd(p, 10);
        const auto s = p.engine()->snapshot();
        was = was || s.restoring;
        if (s.frames > 0 && !s.restoring && s.booted) {
            std::printf("  restore %s, over at %d ms\n", was ? "ran" : "did not run", ms + 10);
            printRows(p);
            return ms + 10;
        }
    }
    return -1;
}

// Sounds, BankSet S8 (BankSet held, Bank 8), then `bank` for S8-1 to S8-9 (openS8 in tests/test_app_media.cpp).
void openS8(TsProcessor& p, ens::ts::Button bank) {
    using ens::ts::Button;
    pressCd(p, Button::Sounds);
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(Button::BankSet), true, 0});
    runCd(p, 100);
    pressCd(p, Button::Bank8);
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(Button::BankSet), false, 0});
    runCd(p, 300);
    if (bank != Button::Bank0) pressCd(p, bank);
    printRows(p);
}

// The sound on the open S8 page (Soft5, as playS8), then C-E-G for 400 ms: the peak of Main.
float playChord(TsProcessor& p) {
    pressCd(p, ens::ts::Button::Soft5);
    juce::AudioBuffer<float> buf(4, 480);
    juce::MidiBuffer midi;
    for (int k : {60, 64, 67}) midi.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8)127), 0);
    float peak = 0;
    for (int i = 0; i < 40; ++i) {
        buf.clear();
        p.processBlock(buf, midi);
        midi.clear();
        peak = std::max({peak, buf.getMagnitude(0, 0, 480), buf.getMagnitude(1, 0, 480)});
    }
    for (int k : {60, 64, 67}) midi.addEvent(juce::MidiMessage::noteOff(1, k), 0);
    run(p, 300, midi);
    std::printf("  C-E-G peak %.4f\n", peak);
    return peak;
}

// A state's <Media>: per medium "kind label blocks", and the total of blocks.
struct MediaSeen {
    juce::StringArray media;
    int blocks = 0;
    const juce::XmlElement* xml = nullptr;  // inside the state passed: keep that alive while this is used
};
MediaSeen mediaOf(const juce::XmlElement* state) {
    MediaSeen m;
    m.xml = state ? state->getChildByName("Media") : nullptr;
    if (m.xml == nullptr) return m;
    for (const auto* e : m.xml->getChildWithTagNameIterator("Medium")) {
        int n = 0;
        for (const auto* r : e->getChildWithTagNameIterator("Run")) n += r->getIntAttribute("count");
        m.media.add(e->getStringAttribute("kind") + " " + e->getStringAttribute("label") + " " + juce::String(n));
        m.blocks += n;
    }
    std::printf("  media: %s (%d blocks)\n", m.media.joinIntoString(", ").toRawUTF8(), m.blocks);
    return m;
}

// The VST3's state before the restore: the sound loaded and the media it came from, in a new processor.
void restoreFrom(TsProcessor& b, const juce::MemoryBlock& s) {
    b.prepareToPlay(48000, 480);
    b.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
}

// project_state_restores_sounds (A129): ED-001's FLUTE 1 into S8-0 and CDR-13's BONGOS 1 into S8-1 by posted presses;
// the state is saved and both images ejected; a new processor given the state, with no image anywhere, has both
// sounds back on their bank pages and plays C-E-G from S8-0. A state taken during that restore holds the saved media
// unchanged (Review Focus 3), and one taken after it holds what the auto-load read: both media again. Returns the
// first state (empty without the images).
juce::MemoryBlock mediaStateRoundTrip(const juce::File& dir) {
    using ens::ts::Button;
    const auto ed = ed001();
    const auto cue = cdr13();
    if (ed == juce::File() || cue == juce::File() || !realRoms(dir)) return {};
    juce::MemoryBlock s;
    {
        TsProcessor a;
        if (!a.engine()) return {};
        a.prepareToPlay(48000, 480);
        juce::String err;
        CHECK(a.drive().open(ed, err));
        runCd(a, kBoot);
        CHECK(loadFromFloppy(a, "FLUTE 1", Button::Bank0));
        CHECK(a.cdDrive().insert(cue, err));
        openScsiStatus(a);
        CHECK(shows(a, "CDR-013"));
        CHECK(loadFromCd(a, {"PERCUSSION", "DRUMS", "BONGOS 1"}, Button::Bank1));
        runCd(a, 200);
        s = stateOf(a);
        CHECK(a.drive().eject() && a.cdDrive().eject());
        runCd(a, 100);
    }
    const auto xml = xmlOf(s);
    const auto saved = mediaOf(xml.get());
    CHECK(saved.media.size() == 2 && saved.media[0].startsWith("floppy-dd ED-001 ") &&
          saved.media[1].startsWith("cd CDR-013 "));
    std::printf("  state: %d bytes\n", static_cast<int>(s.getSize()));

    TsProcessor b;
    if (!b.engine()) return {};
    restoreFrom(b, s);
    runCd(b, 2000);
    CHECK(b.engine()->snapshot().restoring);
    const auto during = xmlOf(stateOf(b));
    const auto mid = mediaOf(during.get());
    CHECK(mid.xml != nullptr && saved.xml != nullptr && mid.xml->isEquivalentTo(saved.xml, false));
    CHECK(runRestore(b) > 0);
    CHECK(b.drive().slotText().isEmpty() && !b.engine()->snapshot().diskIn && b.engine()->snapshot().cdMedium == 0);
    openS8(b, Button::Bank0);  // first: BankSet and Bank8 keep the bank number, and Bank0 is not pressed
    CHECK(shows(b, "FLUTE 1"));
    CHECK(playChord(b) > 0.01f);
    openS8(b, Button::Bank1);
    CHECK(shows(b, "BONGOS 1"));
    runCd(b, 200);
    const auto after = mediaOf(xmlOf(stateOf(b)).get());
    CHECK(after.media.size() == 2 && after.media.joinIntoString(",").contains("ED-001") &&
          after.media.joinIntoString(",").contains("CDR-013"));
    return s;
}

// project_state_keeps_unrestored_media (A129): the round trip's state with one CD sector of BONGOS 1 taken out (the
// middle of the CD's longest run, its sample data): the restore loads FLUTE 1, the OS stops BONGOS 1 at the gap
// (DISK DRIVE NOT RESPONDING, Up, the UNKNOWN prompt, *SKIP*), so the restore does not end clean. The state saved
// after it still holds every restored block: ED-001's and CDR-013's but the one taken out, not only the sectors the
// auto-load read before the gap.
void mediaStateKeepsUnrestored(const juce::File& dir, const juce::MemoryBlock& full) {
    if (full.isEmpty() || !realRoms(dir)) return;
    auto x = xmlOf(full);
    juce::XmlElement* run = nullptr;
    int cdBlocks = 0;
    for (auto* m : x->getChildByName("Media")->getChildWithTagNameIterator("Medium"))
        if (m->getStringAttribute("kind") == "cd")
            for (auto* r : m->getChildWithTagNameIterator("Run")) {
                cdBlocks += r->getIntAttribute("count");
                if (run == nullptr || r->getIntAttribute("count") > run->getIntAttribute("count")) run = r;
            }
    CHECK(run != nullptr && run->getIntAttribute("count") >= 3);
    if (run == nullptr || run->getIntAttribute("count") < 3) return;
    juce::MemoryOutputStream bytes;
    CHECK(juce::Base64::convertFromBase64(bytes, run->getAllSubText()));
    const int first = run->getIntAttribute("first"), count = run->getIntAttribute("count"), mid = count / 2;
    const auto* b = static_cast<const char*>(bytes.getData());
    auto* medium = x->getChildByName("Media")->getChildByAttribute("kind", "cd");
    medium->removeChildElement(run, true);
    for (const auto& [from, n] : {std::pair<int, int>{0, mid}, std::pair<int, int>{mid + 1, count - mid - 1}}) {
        auto* r = medium->createNewChildElement("Run");
        r->setAttribute("first", first + from);
        r->setAttribute("count", n);
        r->addTextElement(juce::Base64::toBase64(b + from * 2048, static_cast<size_t>(n) * 2048));
    }
    std::printf("  CD sector %d taken out of %d\n", first + mid, cdBlocks);
    juce::MemoryBlock s;
    juce::AudioProcessor::copyXmlToBinary(*x, s);
    TsProcessor p;
    if (!p.engine()) return;
    restoreFrom(p, s);
    CHECK(runRestore(p) > 0);
    CHECK(!p.engine()->snapshot().restoreClean);
    runCd(p, 200);
    const auto after = mediaOf(xmlOf(stateOf(p)).get());
    CHECK(after.media.contains("floppy-dd ED-001 314"));
    CHECK(after.media.contains("cd CDR-013 " + juce::String(cdBlocks - 1)));
}

// project_state_after_a_restore_untouched (A129): a restore ends and, before a tick has seen the end, the host gives
// the processor a project again (the timer runs on the message thread; a host may load a state from another). The
// new project's restore must still run: restart must not finish the old restore over the new project's media.
void mediaStateTwiceWithoutTick(const juce::File& dir, const juce::MemoryBlock& full) {
    if (full.isEmpty() || !realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    restoreFrom(p, full);
    juce::AudioBuffer<float> buf(4, 480);
    juce::MidiBuffer none;
    bool ended = false;
    for (int i = 0; i < 13000 && !ended; ++i) {  // as runCd, without tick(), at most 130 s
        buf.clear();
        p.processBlock(buf, none);
        uint32_t medium = 0, lba = 0;
        auto* port = p.engine()->cdPort();
        for (int t = 0; port != nullptr && t < 1000 && port->pending(medium, lba); ++t) juce::Thread::sleep(1);
        const auto s = p.engine()->snapshot();
        ended = s.frames > 0 && !s.restoring && s.booted;
    }
    CHECK(ended);
    p.setStateInformation(full.getData(), static_cast<int>(full.getSize()));
    runCd(p, 2000);
    CHECK(p.engine()->snapshot().restoring);
    CHECK(runRestore(p) > 0);
    openS8(p, ens::ts::Button::Bank0);
    CHECK(shows(p, "FLUTE 1"));
}

// project_state_trims_on_reopen (A129): after FLUTE 1 is loaded from ED-001, CDR-13 is browsed (mounted on the SCSI
// STATUS page, its Load page opened and stepped through the root), loading nothing from it; ED-001 holds no other
// sampled sound to browse past (its Load page steps from FLUTE 1 to *NO-FILES*). So the first state holds blocks no
// sound needs; the state saved after a restore from it holds only what the auto-load read: ED-001's, fewer.
void mediaStateTrimsOnReopen(const juce::File& dir) {
    using ens::ts::Button;
    const auto ed = ed001();
    const auto cue = cdr13();
    if (ed == juce::File() || cue == juce::File() || !realRoms(dir)) return;
    juce::MemoryBlock s;
    {
        TsProcessor a;
        if (!a.engine()) return;
        a.prepareToPlay(48000, 480);
        juce::String err;
        CHECK(a.drive().open(ed, err) && a.cdDrive().insert(cue, err));
        runCd(a, kBoot);
        CHECK(loadFromFloppy(a, "FLUTE 1", Button::Bank0));
        openScsiStatus(a);
        CHECK(shows(a, "CDR-013"));
        pressCd(a, Button::Soft4);  // *LOAD*
        CHECK(until(a, "TYPE=", 5000));
        pressCd(a, Button::Soft1);
        for (int i = 0; i < 4; ++i) pressCd(a, Button::Up);
        printRows(a);
        runCd(a, 200);
        s = stateOf(a);
    }
    const auto first = mediaOf(xmlOf(s).get());
    CHECK(first.media.size() == 2);
    TsProcessor b;
    if (!b.engine()) return;
    restoreFrom(b, s);
    CHECK(runRestore(b) > 0);
    runCd(b, 200);
    const auto second = mediaOf(xmlOf(stateOf(b)).get());
    CHECK(second.media.contains("floppy-dd ED-001 314") && second.blocks > 0 && first.blocks > second.blocks);
}

// A <TSState> built here: version 1, the parameters of a processor with Volume 0.3, a cold RAM (zeros) and `media`.
juce::MemoryBlock syntheticState(std::unique_ptr<juce::XmlElement> media) {
    TsProcessor q(MemoryMode::File);  // its state has the parameters and nothing else
    q.params().getParameter("volume")->setValueNotifyingHost(0.3f);
    auto x = xmlOf(stateOf(q));
    x->setAttribute("version", 1);
    const std::vector<uint8_t> ram(static_cast<size_t>(kMemoryBytes), 0);
    x->createNewChildElement("RAM")->addTextElement(juce::Base64::toBase64(ram.data(), ram.size()));
    if (media) x->addChildElement(media.release());
    juce::MemoryBlock m;
    juce::AudioProcessor::copyXmlToBinary(*x, m);
    return m;
}

// A <Medium> in `media`, and a <Run> of `count` blocks of `bytes` each from `first` (a pattern, or `base64` as given).
juce::XmlElement* addMedium(juce::XmlElement& media, const char* kind, const char* label, int size) {
    auto* m = media.createNewChildElement("Medium");
    m->setAttribute("kind", kind);
    m->setAttribute("label", label);
    m->setAttribute("size", size);
    return m;
}
void addRun(juce::XmlElement& medium, int first, int count, int bytes, const char* base64 = nullptr) {
    auto* r = medium.createNewChildElement("Run");
    r->setAttribute("first", first);
    r->setAttribute("count", count);
    std::vector<uint8_t> data(static_cast<size_t>(count * bytes));
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 7 + static_cast<size_t>(first));
    r->addTextElement(base64 != nullptr ? juce::String(base64) : juce::Base64::toBase64(data.data(), data.size()));
}

// project_state_saved_during_restore (Review Focus 3): a state taken while the restore runs holds the restored <Media>
// as it came, not what the record holds so far. Synthetic media on a cold RAM (no images needed): the restore lasts
// the boot (engine_restore_without_question_ends_at_boot).
void mediaStateDuringRestore(const juce::File& dir) {
    if (!realRoms(dir)) return;
    auto media = std::make_unique<juce::XmlElement>("Media");
    auto* f = addMedium(*media, "floppy-dd", "SYNTH", 1600);
    addRun(*f, 0, 3, 512);
    addRun(*f, 10, 1, 512);
    auto* c = addMedium(*media, "cd", "SYN-CD", 100);
    addRun(*c, 0, 2, 2048);
    const juce::XmlElement want(*media);
    const auto s = syntheticState(std::move(media));
    TsProcessor p;
    if (!p.engine()) return;
    restoreFrom(p, s);
    for (int i = 0; i < 2; ++i) {  // at 1 s and 3 s
        runCd(p, 1000 + 1000 * i);
        CHECK(p.engine()->snapshot().restoring);
        const auto st = xmlOf(stateOf(p));
        const auto got = mediaOf(st.get());
        CHECK(got.xml != nullptr && got.xml->isEquivalentTo(&want, false));
    }
    CHECK(runRestore(p) > 0);
}

// project_state_drops_bad_media (Review Focus 5): bad base64, a block past `size` and kind "zip" each drop that medium;
// the good one, the RAM and the parameters restore.
void mediaStateBadBlobs(const juce::File& dir) {
    if (!realRoms(dir)) return;
    auto media = std::make_unique<juce::XmlElement>("Media");
    addRun(*addMedium(*media, "floppy-dd", "BAD64", 1600), 0, 2, 512, "!!not base64!!");
    addRun(*addMedium(*media, "floppy-dd", "PAST", 1600), 1599, 2, 512);
    addRun(*addMedium(*media, "zip", "ZIP", 1600), 0, 2, 512);
    addRun(*addMedium(*media, "cd", "CD-PAST", 10), 0, 11, 2048);
    addRun(*addMedium(*media, "floppy-hd", "GOOD", 3200), 0, 4, 512);
    const auto s = syntheticState(std::move(media));
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, 100);
    const auto* engine = p.engine();
    p.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
    CHECK(p.engine() != engine);  // the RAM restarted the machine
    runCd(p, 200);
    CHECK(feq(*p.params().getRawParameterValue("volume"), 0.3f));
    const auto st = stateOf(p);
    const auto ram = ramOf(st);
    CHECK(ram.size() == static_cast<size_t>(kMemoryBytes) && std::all_of(ram.begin(), ram.end(), [](uint8_t b) {
              return b == 0;
          }));
    const auto got = mediaOf(xmlOf(st).get());
    CHECK(got.media.size() == 1 && got.media[0] == "floppy-hd GOOD 4");
    CHECK(p.engine()->snapshot().restoring);
    CHECK(runRestore(p) > 0);
}

// standalone_has_no_media (A121): a File processor that loaded FLUTE 1 records nothing and its state has no <Media>.
void standaloneHasNoMedia(const juce::File& dir) {
    const auto ed = ed001();
    if (ed == juce::File() || !realRoms(dir)) return;
    TsProcessor p(MemoryMode::File);
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    juce::String err;
    CHECK(p.drive().open(ed, err));
    runCd(p, kBoot);
    CHECK(loadFromFloppy(p, "FLUTE 1", ens::ts::Button::Bank0));
    runCd(p, 200);
    CHECK(p.record().media().empty());
    const auto xml = xmlOf(stateOf(p));
    CHECK(xml != nullptr && xml->getChildByName("Media") == nullptr);
}

// The built VST3 through JUCE's VST3 host: TSR_SETTINGS_DIR points the plugin at a temp settings file
// naming the ROM folder, so neither the user's settings nor their memory file is touched. Aux is off,
// as the Standalone plays it (A109). With `ownRoms` (the release VST3, A125, A126) the settings folder names
// no ROM folder: the plugin must boot on the ROMs in its own package, and must not store that folder.
void vst3(const juce::File& dir, const juce::File& bundle, bool ownRoms) {
    if (!bundle.exists()) {
        std::printf("  skip: VST3 not found: %s\n", bundle.getFullPathName().toRawUTF8());
        CHECK(!ownRoms);  // a release VST3 that was asked for must be there
        return;
    }
    if (!ownRoms && !haveRoms()) return;
    setSettingsDir(dir);  // the plugin DLL reads this process's environment, so it lands here too
    if (!ownRoms) AppSettings().setRomFolder(juce::File(TSR_ROM_DIR));

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

    // The host-side editor makes the plugin's IPlugView, which builds the panel editor at its default size
    // (A112), and closes it again without a crash.
    std::unique_ptr<juce::AudioProcessorEditor> ed(plugin->createEditorAndMakeActive());
    CHECK(ed != nullptr);
    if (ed) {
        std::printf("  VST3 editor: %d x %d\n", ed->getWidth(), ed->getHeight());
        CHECK(ed->getWidth() == 1546 && ed->getHeight() == 552 && !ed->isOnDesktop());
    }
    ed.reset();
    plugin->releaseResources();
    plugin.reset();
    if (ownRoms) CHECK(AppSettings().romFolder() == juce::File());
}

}  // namespace

// DECISION A133: the firmware's MIDI out leaves processBlock as whole messages; the input is not echoed.
void midiOut(const juce::File& dir) {
    if (!realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, kBoot);
    juce::AudioBuffer<float> buf(4, 480);
    bool on = false, echoed = false;
    p.engine()->post({ens::app::Event::Kind::Key, 60, true, 100});
    for (int i = 0; i < 20; ++i) {
        juce::MidiBuffer m;
        if (i == 0) m.addEvent(juce::MidiMessage::controllerEvent(1, 0x50, 0x11), 0);  // an undefined CC
        buf.clear();
        p.processBlock(buf, m);
        for (const auto e : m) {
            const auto msg = e.getMessage();
            on |= msg.isNoteOn() && msg.getNoteNumber() == 60;
            echoed |= msg.isController() && msg.getControllerNumber() == 0x50;
        }
    }
    p.engine()->post({ens::app::Event::Kind::Key, 60, false, 100});
    run(p, 100);
    CHECK(p.producesMidi() && on && !echoed);
}

// DECISION A134: the outputs are AC-coupled, as the TS's analog board: after a note dies away the machine's Main
// sits a few LSB below 0 (OT12, T11 truncation), and the host output does not.
void dcBlocked(const juce::File& dir) {
    if (!realRoms(dir)) return;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, kBoot);
    juce::MidiBuffer on, off;
    on.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<uint8_t>(100)), 0);
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    run(p, 500, on);
    run(p, 10000, off);
    juce::AudioBuffer<float> buf(4, 480);
    double sum[2] = {0, 0};
    for (int b = 0; b < 100; ++b) {
        juce::MidiBuffer none;
        buf.clear();
        p.processBlock(buf, none);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 480; ++i) sum[c] += buf.getSample(c, i);
    }
    const double lsb[2] = {sum[0] / 48000 * 32768, sum[1] / 48000 * 32768};
    std::printf("  Main mean after the tail: %.3f %.3f LSB\n", lsb[0], lsb[1]);
    CHECK(std::abs(lsb[0]) < 0.25 && std::abs(lsb[1]) < 0.25);
}

// DECISION A135: the app's machine has 8 MB of sample RAM: BankSet + Bank 9 selects S9 in Sounds mode.
void sampleRam8(const juce::File& dir) {
    if (!realRoms(dir)) return;
    using ens::ts::Button;
    TsProcessor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, kBoot);
    press(p, Button::Sounds);
    const auto post = [&](Button b, bool down) {
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), down, 0});
        run(p, 100);
    };
    post(Button::BankSet, true);
    post(Button::Bank9, true);
    post(Button::Bank9, false);
    post(Button::BankSet, false);
    run(p, 300);
    CHECK(rowStarts(p, 0, "S9-0"));
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;  // the VST3 host and editor need the message manager
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
    follow(dir.getChildFile("follow2"));
    const auto r2 = bankState(dir.getChildFile("bank"));
    bankRestores(dir.getChildFile("bank-restores"), r2);
    standaloneStateBeforeBoot(dir.getChildFile("standalone-state"), r2);
    dataEntryRestored(dir.getChildFile("data-entry"), r2);
    zeroAndBypassedBlocks(dir.getChildFile("zero"));
    romsBesideApp(dir.getChildFile("beside"));
    romFolderOrder(dir.getChildFile("order"));
    romFolderInUse(dir.getChildFile("inuse"));
    auxBusOff(dir.getChildFile("buses"));
    midiOut(dir.getChildFile("midi-out"));
    dcBlocked(dir.getChildFile("dc"));
    sampleRam8(dir.getChildFile("simm8"));  // not "aux": a reserved device name on Windows
    editor(dir.getChildFile("editor"));
    diskDrive(dir.getChildFile("disk"));
    cdDrive(dir.getChildFile("cd"));
    cdDriveLoads(dir.getChildFile("cd-loads"));
    cdDriveMissingFile(dir.getChildFile("cd-missing"));
    dropGoesToCd(dir.getChildFile("drop"));
    mediaStateDuringRestore(dir.getChildFile("media-during"));
    mediaStateBadBlobs(dir.getChildFile("media-bad"));
    standaloneHasNoMedia(dir.getChildFile("media-standalone"));
    const auto media = mediaStateRoundTrip(dir.getChildFile("media-round-trip"));
    mediaStateKeepsUnrestored(dir.getChildFile("media-keep"), media);
    mediaStateTwiceWithoutTick(dir.getChildFile("media-twice"), media);
    mediaStateTrimsOnReopen(dir.getChildFile("media-trim"));
    vst3(dir.getChildFile("vst3"), juce::File(TSR_VST3_PATH), false);
    // TSR_RELEASE_VST3=<a release folder's TS Reborn.vst3>: that package, on its own ROMs (A125, A126).
    const auto release = juce::SystemStats::getEnvironmentVariable("TSR_RELEASE_VST3", {});
    if (release.isEmpty())
        std::printf("  skip: TSR_RELEASE_VST3 not set\n");
    else
        vst3(dir.getChildFile("release"), juce::File(release), true);
    dir.deleteRecursively();
    std::printf(failures == 0 ? "app_tests: all passed\n" : "app_tests: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
