# TS Reborn JUCE App Implementation Plan (Plan 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** "TS Reborn", a Standalone and a VST3 for Windows x64 and Linux x64. The user plays the emulated TS-10 from a MIDI keyboard or a DAW, uses its whole front panel on screen, and a DAW project brings each instance back as it was left.

**Architecture:**
- **Copy and adapt, as Plan 1 did with the cores.** KS-32 Reborn (`C:\Users\abart\Desktop\Ensoniq KS-32`, commit `c17c35c`) has finished all of this for the KS-32 (its Plans 7 and 8). Its engine, app and tests are copied and changed where the TS differs. Every change is listed in the task that makes it.
- **A JUCE-free engine, `ens_app`** (`src/app/`). It owns one `ts::Machine` and a 4-channel resampler. It takes MIDI with block offsets and GUI events through a fixed ring, renders Main and Aux at the host rate, publishes a panel snapshot, tracks the selected sound and replays it after a boot.
- **A JUCE-free layout table** (`src/app/panel_layout.*`): the TS-10 front panel measured from `datasheets/ts10-photo.webp`, labelled after Musician's Manual page 3.
- **The JUCE layer** (`app/`): one `AudioProcessor` owning the engine, a panel editor drawn from the table, a settings page, the battery-memory file, host parameters and plugin state.

**Tech Stack:**
- C++17 and CMake 3.20+, with `tests/check.h`.
- JUCE 8.0.16-security, behind `ENS_BUILD_APP`. Not downloaded: the KS-32 checkout at `C:/Users/abart/Desktop/Ensoniq KS-32/build-juce/juce-src` is used through `-DFETCHCONTENT_SOURCE_DIR_JUCE`.
- KS-32's `ens::player::Resampler`, copied.
- Linux: Ubuntu in WSL (`wsl -d Ubuntu`), GCC and Ninja, as KS-32 builds there.

**Spec:** `docs/design/2026-11-13-ts-emulator-design.md` ("design"): sections 2.4, 3, 10 and 11.4. The model is KS-32 `docs/superpowers/plans/2026-10-28-ks32-juce-app.md` (KS-32 Plan 7) and `2026-11-04-ks32-vst3-release.md` (KS-32 Plan 8), with their decisions A3-A23 in KS-32 `docs/decisions.md`.

## Global Constraints

- **Rules from AGENTS.md, unchanged.** Run the firmware, never study it. MAME is not a reference. No JUCE, OS calls or file I/O under `src/` (`src/app` may use `float`, `std::atomic` and `std::mutex`; `src/ts` and the chip cores stay integer-only). Tests never touch the user's own settings or memory.
- **Determinism.** These hashes stay identical on MSVC x64, MinGW i686 and Linux GCC: boot trace `3bfc7336098a6def`; renders `4a751109deae8b89` (`player_smoke.mid`), `524fcc64af78f175` (`full_chord.mid`), `01e3c8622cc8c8be` (`filtered_patch.mid`).
- **Builds.** Everything except `app/` builds without warnings on MSVC x64 (`build-msvc/`, `/W4`) and MinGW (`build/`, `-Wall -Wextra`; put `C:\winlibs\mingw32\bin` on PATH first). `app/` builds with `-DENS_BUILD_APP=ON` on MSVC x64 (`build-app/`) and on Linux GCC (`build-linux-app/`, in WSL), with no warnings from our own files.
- **Real time.** Once booted, nothing `Engine::render` or `processBlock` calls may allocate, lock (other than `try_lock`) or wait. `ens_rt_tests` enforces it.
- **No downloads.** JUCE comes from the KS-32 checkout. If it is missing, the controller asks the user before fetching anything.
- **Names.** Product "TS Reborn" (design section 8). JUCE target `TSReborn`, `COMPANY_NAME "TS Reborn"`, `BUNDLE_ID com.tsreborn.tsreborn`, `PLUGIN_MANUFACTURER_CODE Tsre`, `PLUGIN_CODE Tsrb`, `VERSION 0.4.0`. The UI shows the "TS-10" model decal; no Ensoniq name or logo anywhere.
- **Settings.** `%APPDATA%\TS Reborn\settings.xml` (Linux: `~/.config/TS Reborn/`), the memory file `ts-memory.bin` beside it. The environment variable `TSR_SETTINGS_DIR` replaces the folder; every test and every app check sets it to a temporary folder.
- **ROM tests** read `ENS_ROM_DIR`; without the ROMs a test prints `  skip: ROMs not found` and passes.
- **Decisions.** Each default below gets `// DECISION A1xx: <one line>` at the point of use and a `## A1xx: <title>` entry in `docs/decisions.md` under a new `# App` heading, in the existing format (Options, Choice, Evidence, Confidence, Status). Where a KS-32 decision is carried over, the Evidence names it ("as KS-32 A5").
- **Stop and ask.** If the firmware or the code contradicts the design, the implementer stops and reports.
- **Branch.** `plan4-juce-app`, a commit per task, merged into `main` with a merge commit after the final review and the user's yes.

## Decision Defaults

| Id | Topic | Default (KS-32 origin) | Task |
|---|---|---|---|
| A100 | Frame rate | The engine's input rate is `kCpuHz / 512.0` (29 761.9 Hz, 32 voices, T1). `kFrameHz` in `src/ts/machine.h`. | 2 |
| A101 | MIDI timing in a block | A byte at output offset `o` enters before machine frame `floor(o * kFrameHz / outRate)` of the block; the block store holds 1024 bytes, extra bytes dropped and counted (A3). | 2 |
| A102 | GUI events | Applied at the next block start, in order; queue of 256; `post` false when full (A4). | 2 |
| A103 | Snapshot hand-off | `try_lock` on the audio side, a skipped block if the GUI holds it (A5). | 2 |
| A104 | Booted | After `kBootMs` (5300 ms) of emulated time since power-on (A6). | 2 |
| A105 | Sound tracking and restore | As KS-32 A15/A18: the last selection (bank select `Bn 00 mm`, `Bn 20 ll`, then `Cn pp`) seen after the boot on MIDI out or MIDI in is the current sound; a requested sound or saved selection is replayed into MIDI in at the first block start once booted. The boot ends on U0-0 (program 0, bank 0/0). | 2 |
| A106 | Analogs after the boot | The host's analog values reach the machine only once booted; until then `kAnalogRest` (A16). | 2 |
| A107 | MIDI note mask | As KS-32 A19. | 2 |
| A108 | MIDI in | Every byte of every `MidiBuffer` message goes to MIDI IN as the jack would carry it (A12). The firmware itself takes Poly-Key and channel pressure from MIDI (Musician's Manual, MIDI Control "PRESSURE": "Either Channel pressure or Poly-Key pressure will be received via incoming MIDI"), so the engine converts nothing. This replaces design section 10's pressure line. | 2 |
| A109 | Outputs | Two stereo output buses, Main and Aux. When the Aux bus is disabled or has no channels, Aux is added into Main. The Standalone's default layout has Aux disabled, so it plays both on one stereo pair (design section 10). | 4 |
| A110 | Analog positions | 0-1 control positions, 0 = bottom or toward the player, `lround(p * 1023)`; nothing is inverted on the TS (interface doc, "Analog inputs"). Data Entry is clamped to 1-1023: raw 0 makes the OS rewrite the selected value and show SYSTEM ERROR 144. Defaults are the `kAnalogRest` positions. | 3 |
| A111 | Parameters | `volume`, `dataEntry`, `pitchWheel`, `modWheel`, `cvPedal` (floats 0-1) and `sound` (int 0-127), as KS-32 A17 plus the CV pedal. | 5 |
| A112 | Layout units | The photo scaled to 2576 x 920 design units. Default window 1546 x 552, minimum 1030 x 368, aspect fixed (A13). | 3, 6 |
| A113 | Display | Codes 0x20-0x7E drawn as ASCII in a monospaced font, others blank (A7). Underlined cells draw a bar under the character. Blink at 2 Hz, 50 % duty, for flashing cells (the underline if underlined, else the character) and flashing LEDs (A8). | 6 |
| A114 | Chord latch and stuck buttons | As KS-32 A9. | 6 |
| A115 | Key strike | `1 + round(126 * y / keyLength)`, y from the key's back edge: the front is loud (A10). The value is the keyboard's strike value (`Machine::key`), which the OS maps through its TOUCH table. | 6 |
| A116 | Memory file | As KS-32 A11: saved on close and every 60 s, never before booted; via `<file>.tmp`; Erase loads 512 KB of zeros after a confirmation. | 4 |
| A117 | State, RAM and memory per format | As KS-32 A20-A23, with `<TSState version="1">` and a `<RAM>` of 524 288 bytes (the 512 KB battery image, T13). | 5 |

## Review Focus

1. **The audio thread stalls**: the GUI reads the snapshot during a block, the autosave asks for RAM, a MIDI flood arrives. Tests: `rt_machine_runframe_does_not_allocate` (Task 1), `rt_engine_render_does_not_allocate` (Task 2).
2. **The host changes the rate or the block size mid-run**, or turns the Aux bus off and on. No allocation, no overrun, oversize blocks rendered in pieces, Aux mixed or routed per A109. Tests: `engine_render_larger_than_prepared_block` (Task 2), `processor_aux_bus_off_mixes_into_main` (Task 4).
3. **A project saved during the boot, or opened with the ROMs missing.** The state keeps the start image and the selection; nothing half-booted is saved; the state round-trips unchanged without ROMs. Tests: `state_saved_during_boot_keeps_start_image`, `state_without_roms_round_trips` (Task 5).
4. **Data Entry at its bottom.** Dragging the slider or a host lane to 0 must never send raw 0. Test: `analog_map_data_entry_never_zero` (Task 3).
5. **A note right after a sound change.** The OS ignores notes for about 50 ms after a program change that loads a new effect (found after Plan 3: the effect reload; Musician's Manual "Switching Effects in Song Mode"). The restore at the boot's end must not land on the user's first notes, and the docs must say so. Test: `engine_restore_happens_once_at_boot_end` (Task 2); the docs line in Task 7.

---

### Task 1: Real-time safety proven

**Files:**
- Create: `tests/rt_main.cpp` (copied from KS-32, the `ens_rt_tests` executable)
- Modify: `tests/CMakeLists.txt` (add `ens_rt_tests` and its CTest entry), `src/ts/machine.h` (threading contract comment), `docs/decisions.md` (A1 and A2's Status: name the TS files)

**Interfaces:**
- Produces: `ens_rt_tests` with `allocStart()` / `allocStop()` (returns the count) for Task 2.

- [ ] **Step 1: Write the failing test.** Copy KS-32 `tests/rt_main.cpp`. Keep its counting `operator new`/`delete` and its runner, drop its KS-32 tests, and add `rt_machine_runframe_does_not_allocate` (needs the ROMs; skip without): boot `kBootMs`, then count allocations over 1 s of `runFrame` while calling, every 50 ms: `midiIn` of `90 3C 64` and `80 3C 00`; `button(Button::Up, true)` then `false`; `key(60, true, 64)` then `key(60, false, 64)`; `pressure(60, 40)`; `setAnalog(Analog::ModWheel, 0x100)`; `patchSelect(0, true/false)`; `popMidiOut` until empty; a copy of `panel()`. Expect 0.
- [ ] **Step 2: Run it.** `cmake --build build-msvc --config Release` then `build-msvc\Release\ens_rt_tests.exe`. It must fail first only if something allocates; if it passes at once, change one call to allocate (e.g. a `std::string` copy of a row) to watch it fail, then remove that.
- [ ] **Step 3:** Fix anything it finds allocating in `runFrame` and the calls above, without changing behaviour. Add above `class Machine` in `machine.h`: every call on one `Machine` comes from one thread at a time; the engine owns it on the audio thread after the boot.
- [ ] **Step 4:** Both builds: `ens_tests` 291 passed and the boot hash unchanged; `ens_rt_tests` 1 passed, allocations 0.
- [ ] **Step 5:** Commit: `test(ts): ens_rt_tests: runFrame and the input calls do not allocate`.

### Task 2: The engine (`ens_app`)

**Files:**
- Create: `src/app/spsc_ring.h`, `src/app/engine.h`, `src/app/engine.cpp`, `src/app/resampler.h`, `src/app/resampler.cpp`, `tests/test_app_engine.cpp`
- Modify: `CMakeLists.txt` (`ens_app` = engine + resampler, linked to `ens_ts`), `tests/CMakeLists.txt`, `tests/rt_main.cpp`, `src/ts/machine.h` (`kFrameHz`, `seqLed()`, `analog()`), `docs/decisions.md` (A100-A108), `docs/ts-hardware-interface.md` ("MIDI": the selection bytes)

**Interfaces:**
- Consumes: Task 1's `allocStart`/`allocStop`.
- Produces, in `namespace ens::app`: KS-32's `engine.h` with `ks32::` replaced by `ts::` and these differences:

```cpp
struct Event {
    enum class Kind : uint8_t { Button, Key, Analog, PatchSelect };
    Kind kind; uint8_t id; bool down; uint16_t value;
    // Button: ts::Button code. Key: note 36-96, value = strike 1-127. Analog: ts::Analog, value = raw.
    // PatchSelect: id 0 = left, 1 = right.
};
struct Snapshot {  // KS-32's fields, plus:
    ts::PanelState panel{};
    bool seqLed = false;  // the Seqs/Songs LED: DUART OP3 low (interface doc, "Display and LEDs")
    // booted, frames, midiDropped, midiOutBytes, program, select[8], selectLen, notes[2],
    // programsSent, programSeq as in KS-32
};
class Engine {  // KS-32's API, plus:
    void render(float* mainL, float* mainR, float* auxL, float* auxR, int frames);  // replaces render(l, r, n)
    void setAnalog(ts::Analog a, uint16_t raw);  // audio thread, applied at a block start once booted
};
```

  `constexpr double kFrameHz = ts::kCpuHz / 512.0;` in `engine.h` (A100; `src/ts` stays integer-only). In `src/ts/machine.h`: `bool Machine::seqLed() const` (OP3 pin low, from `duart().outputPins()`), `uint16_t Machine::analog(Analog a) const` (the value last set, `kAnalogRest` until then).

- [ ] **Step 1: Copy.** `src/app/spsc_ring.h` and `engine.{h,cpp}` from KS-32 `src/app/`; `src/player/resampler.{h,cpp}` to `src/app/resampler.{h,cpp}` with the namespace changed to `ens::app`. `tests/test_app_engine.cpp` from KS-32.
- [ ] **Step 2: Change the tests to the TS.** Keep every KS-32 test name; replace the KS-32 facts with the TS ones:
  - `engine_create_rejects_wrong_rom_size`: `osHigh` one byte short; `err` names `osHigh`.
  - `engine_matches_machine_and_resampler`: four channels; `Resampler(kFrameHz, 48000, 4)` fed `mainL mainR auxL auxR`; the four outputs equal (`==` on floats) for 48 000 frames.
  - `engine_boot_snapshot`: not booted before 5000 ms; after 5400 ms booted, `rowText(panel, 0)` starts `U0-0 BABY-GRAND`, LED bits 0 and 9 set (`leds == 0x201`).
  - `engine_chord_reaches_version_screen` becomes `engine_buttons_reach_system_page`: post `System` down and up, 300 ms; row 0 starts `SYSTEM  TUNE=`.
  - `engine_tracks_panel_pick`: post `Soft2` down/up; `program == 1`, `select` is `B0 00 00 20 00 C0 01` (7 bytes).
  - `engine_tracks_midi_in_program`: `midi(0, {C0 05})`, 500 ms: `program == 5`, row 0 starts `U0-0`, `programsSent == 0`.
  - `engine_restores_program_after_warm_boot`: A takes `C0 20`, 500 ms (display `U0-5`); B from A's RAM with `setProgram(32)` every block: after 5400 + 500 ms row 0 starts `U0-5`, `program == 32`, `programsSent == 1`.
  - `engine_restores_bank_and_preset`: with `setSelect`, two round trips: `BankSet` twice, `Bank2`, `Soft3` (sent `B0 00 00 20 02 C0 0E`, display `R2-2`) and `Presets`, `Bank3`, `Soft4` (sent `B0 00 00 20 00 C0 51`, row 1 starts `PSET`). After B's boot plus 500 ms, the display matches A's.
  - `engine_panel_pick_is_not_undone`, `engine_analog_waits_for_boot` (pitch wheel 0x100 against the 0x200 rest, checked at 1000 and 5400 ms), `engine_note_mask`, `engine_midi_offset_places_note`, `engine_midi_overflow_is_counted`, `engine_ram_handoff`, `engine_render_larger_than_prepared_block`, `spsc_ring_fifo_and_full`: as KS-32, with the TS boot time.
  - New `engine_seq_led`: post `SeqsSongs` down/up: `seqLed` true; `Sounds` down/up: false.
  - New `engine_patch_select_reaches_machine`: on a booted engine, post `Event{PatchSelect, 0, true}`; within 100 ms `snapshot().midiOutBytes` rises by at least 3 (the OS sends `B0 46 40`, as `ts_patch_select_is_read` shows), and by 0 over 100 ms without the post.
  - New `engine_restore_happens_once_at_boot_end`: B as in the warm-boot test; `programsSent` is 0 at 5250 ms and 1 from 5400 ms to 6000 ms; a note sent at 5500 ms is heard.
  - In `rt_main.cpp`, `rt_engine_render_does_not_allocate`, as KS-32's with the Plan 8 additions (`C0 05`, `setProgram(9)`, `setAnalog(Volume, 0x200)`, notes), four output pointers.
- [ ] **Step 3: Run and see them fail.** Build both; expect compile errors (`ts::` types, `render` with four pointers, `seqLed`, `analog`, `kFrameHz`).
- [ ] **Step 4: Implement.** In `engine.cpp`: `ts::Machine`, `kFrameHz` for every rate, a 4-channel resampler and output buffer (reserve in `prepare` so nothing reallocates), the frame loop collecting `Audio{mainL, mainR, auxL, auxR}`, `Event::Kind::PatchSelect` to `patchSelect`, the snapshot's `seqLed`. The tracker needs no change for the TS selection form (running status `B0 00 mm 20 ll` then `Cn pp`); confirm it with the tests. Mark A100-A108.
- [ ] **Step 5:** Both builds: `ens_tests` all pass (291 + the new ones), the four hashes unchanged; `ens_rt_tests` 2 passed, allocations 0.
- [ ] **Step 6:** Add A100-A108 to `docs/decisions.md`. Add to the interface doc's "MIDI" section: a panel pick sends `B0 00 00 20 ll C0 pp` (U0 sounds LSB 0, ROM bank set R2 LSB 2; presets at `pp` 60 + 6 x bank + slot), found by pressing the buttons and reading MIDI out; a warm boot keeps the System settings but starts on U0-0. Commit: `feat(app): JUCE-free engine: Main and Aux, MIDI offsets, panel events, sound restore (A100-A108)`.

### Task 3: Layout table and analog map

**Files:**
- Create: `src/app/panel_layout.h`, `src/app/panel_layout.cpp`, `src/app/analog_map.h`, `tests/test_app_panel_layout.cpp`
- Modify: `CMakeLists.txt` (`panel_layout.cpp` in `ens_app`), `tests/CMakeLists.txt`, `docs/decisions.md` (A110, A112)

**Interfaces:**
- Produces, in `ens::app`:

```cpp
constexpr int kDesignW = 2576, kDesignH = 920;  // A112
struct Rect { int x, y, w, h; };
enum class ControlKind : uint8_t { Button, Led, Slider, Wheel, Patch, Display, Key, Label, Decor };
enum class Ink : uint8_t { Text, Bright, Dim, Decal };
struct Control { ControlKind kind; int id; Rect r; const char* label; Ink ink = Ink::Text; };
// Button: ts::Button code, or kSettingsId. Led: 0-15, or kSeqLedId. Slider, Wheel: ts::Analog.
// Patch: 0 left, 1 right. Key: note 36-96. Display, Label, Decor: -1.
constexpr int kSettingsId = -4, kSeqLedId = 16;
const std::vector<Control>& panelLayout();
uint16_t analogRaw(ts::Analog a, float position);   // A110: lround(p * 1023), NaN -> rest, clamped; DataEntry >= 1
float analogPosition(ts::Analog a, uint16_t raw);
```

- [ ] **Step 1: Write the failing tests** in `tests/test_app_panel_layout.cpp`:
  - `panel_layout_has_every_button`: every `kButtons` entry except `FootSwitch1Left` and `FootSwitch1Right` appears exactly once as a Button; one Button has `kSettingsId`.
  - `panel_layout_has_every_led_and_key`: Leds 0-15 and `kSeqLedId` once each; Keys 36-96 once each; Patch 0 and 1 once each; Sliders `Volume` and `DataEntry`, Wheels `PitchWheel` and `ModWheel`, one Display.
  - `panel_layout_fits_and_does_not_overlap`: every rect inside 2576 x 920; no two Button, Slider, Wheel, Patch or Key rects overlap, except black keys over white keys.
  - `panel_layout_led_matches_button`: each LED's centre within 40 units of its button's: 0 Bank0, 8 Bank1, 2-5 Bank2/4/6/8, 10-13 Bank3/5/7/9, 1 Presets, 9 Sounds, 6 ReplaceTrackSound, 7 Tracks1to6, 15 Tracks7to12, 14 Compare, `kSeqLedId` SeqsSongs.
  - `analog_map_round_trips_rest_values`: for PitchWheel, ModWheel, Volume, CvPedal and DataEntry, `analogRaw(a, analogPosition(a, kAnalogRest[a])) == kAnalogRest[a]`; `analogRaw(PitchWheel, 0.5f) == 0x200`; `analogRaw(Volume, 1.0f) == 0x3FF`; `analogRaw(ModWheel, 0.0f) == 0`.
  - `analog_map_data_entry_never_zero`: `analogRaw(DataEntry, 0.0f) == 1`, also for `-1.0f` and `-INFINITY`; `analogRaw(Volume, 0.0f) == 0`.
- [ ] **Step 2: Run and see them fail.**
- [ ] **Step 3: Write `analog_map.h`** (copy KS-32's, no inversion, the DataEntry floor, A110) **and `panel_layout.cpp`.** Convert the photo to PNG in the scratchpad (`python -c "from PIL import Image; Image.open('datasheets/ts10-photo.webp').resize((2576, 920)).save(r'<scratch>\photo.png')"`) and measure on it. The anchors below are from that image (x, y, w, h); refine by up to about 10 units where the photo shows otherwise. Labels are the photo's, in its line breaks; tooltips use the Musician's Manual names.
  - **Decor:** the disk drive 108,112,310,233 (drawn as an empty slot); the decal plate 2010,150,375,120 with the label "TS-10" (Ink::Decal) and "Performance/Composition Synthesizer" under it.
  - **Sliders:** Volume 478,145,62,158 and Data Entry 680,145,62,158, labels "Volume" and "Data Entry" at y 342. **Up / Down:** 598,208,44,26 and 598,266,44,26, drawn as arrows.
  - **Display:** window 795,115,607,160; text 2 rows x 40 columns in 820,170,565,58. **Soft 1-3** above, centres x 958, 1116, 1288, rects 44 x 16 at y 124; **Soft 4-6** the same at y 250.
  - **Mode row** at y 315, 37 x 28, x 800, 839, 878, 917: Seqs/Songs, Presets, Sounds, BankSet (labels under, y 346). LEDs 10 x 6 at y 300 over the first three (`kSeqLedId`, 1, 9).
  - **Bank 0-9** at y 315, 37 x 28, x = 998 + 39.5 n; a digit label and the LED over each (y 286 and 300); "Bank" to the left; under them "User RAM BankSets" (banks 0-1), "ROM BankSets" (2-7), "Sampled Sound BankSets" (8-9).
  - **Three button blocks**, buttons 36 x 14 in rows at y 138, 193 and 248 with two-line labels above each, and a fourth row of 36 x 26 buttons at y 316:
    - "Track Parameters" (title at 1465,100), columns x 1466, 1506, 1546: Mix/Pan, Attack/Release, Brightness/Timbre; Key Zone/Velocity, Tuning, Controllers On/Off; Performance Options, Track MIDI, Track Effects; then Replace Track Sound, 1-6, 7-12 with LEDs 6, 7, 15 over them and "Seq/Song Tracks" under 1-6 and 7-12.
    - Columns x 1618, 1658, 1698: System, Storage (Disk), MIDI Control; Edit Song, Edit Sequence, Edit Track; Click, Sequencer Control, Locate; then Record, Stop (with "Continue" under it), Play.
    - "Programming" (title at 1765,100), columns x 1766, 1806, 1846, 1886: LFO, Env 1, Env 2, Env 3; Pitch, Pitch Mods, Filters, Output; Wave, Mod Mixer, Program Control, Program Effects; then Select Voice, Copy, Write Program, Compare with LED 14 over Compare.
  - **Settings gear** 2330,80,30,30.
  - **Left of the keys:** Patch 0 at 110,612,33,20 and Patch 1 at 150,612,33,20; the pitch wheel 135,688,42,195 and the mod wheel 222,688,42,195.
  - **Keys** 36-96 across 335..2475 at y 555, 350 tall: 36 white keys of 2140 / 36 units; black keys 0.6 of that wide and 0.62 as long, centred on the gaps as the photo shows.
- [ ] **Step 4:** Both builds: all pass. Commit: `feat(app): TS-10 panel layout table and analog map (A110, A112)`, with A110 and A112 in `docs/decisions.md`.

### Task 4: The JUCE app: processor, settings, ROM folder, memory file

**Files:**
- Create: `app/CMakeLists.txt`, `app/source/Processor.{h,cpp}`, `app/source/AppSettings.{h,cpp}`, `app/source/RomFolder.{h,cpp}`, `app/source/MemoryFile.{h,cpp}`, `app/source/SettingsPage.{h,cpp}`, `app/source/PanelEditor.{h,cpp}` (a placeholder until Task 6), `app/tests/app_tests.cpp` — each copied from KS-32 `app/` and adapted
- Modify: `CMakeLists.txt` (`option(ENS_BUILD_APP "Build the JUCE app (MSVC x64 or Linux x64)" OFF)`, `add_subdirectory(app)` under it, as KS-32), `docs/decisions.md` (A109, A116)

**Interfaces:**
- Consumes: `ens::app::Engine` (Task 2).
- Produces: `class TsProcessor : public juce::AudioProcessor, private juce::Timer` with KS-32 `Ks32Processor`'s public API (engine, romError, setRomFolder, eraseMemory, saved, tick, MemoryMode, …) under the new name. `struct RomFileStatus` and `checkRomFolder(const juce::File&, ts::RomSet*)` checking `os/ts10_310h.bin` (262 144), `os/ts10_310l.bin` (524 288, or 262 144 as `Machine::create` accepts), `waves/901401_u41.bin` (2 097 152), `waves/901301_u43.bin` (1 048 576), `waves/901402_u45.bin` (2 097 152), `waves/901302_u46.bin` (1 048 576): the repo's `roms/` layout, so the user picks that folder.

- [ ] **Step 1: Copy and rename.** KS-32 `app/` into `app/`. Apply the Global Constraints' names, `TSR_SETTINGS_DIR`, the "TS Reborn" folder and `ts-memory.bin`, `ts::` types, the six files above, the 512 KB image (Erase loads 524 288 zeros, A116). `app/CMakeLists.txt` keeps KS-32's `FetchContent_Declare` (tag `8.0.16-security`) so the source dir override applies; the test target is `TSReborn_AppTests` with `TSR_ROM_DIR` and `TSR_VST3_PATH`. Leave out, for Task 5, the parameters and the state; for Task 6, the panel drawing (the placeholder editor shows the two display rows as text and a settings button).
- [ ] **Step 2: Two buses (A109).** `BusesProperties()` with output "Main" (stereo, enabled) and "Aux" (stereo, enabled except under `wrapperType_Standalone`). `isBusesLayoutSupported`: Main stereo; Aux stereo or disabled. `processBlock` renders into four buffers sized in `prepareToPlay` and adds Aux into Main when the Aux bus is off.
- [ ] **Step 3: Adapt `app_tests.cpp`.** Keep KS-32's ROM-folder, memory-file and guard checks with the TS names and sizes (dummy files of zeros, never ROM content). Add `processor_aux_bus_off_mixes_into_main`: on the real ROMs (skip without), two processors get the same blocks and `90 3C 7F` after the boot, one with Aux disabled and one with it enabled; over 500 ms the first's Main equals the second's Main + Aux, sample for sample. Drop the KS-32 checks that need Tasks 5 and 6 (parameters, state, editor); Task 5 and Task 6 bring their own.
- [ ] **Step 4: Build on Windows:**
  `cmake -S . -B build-app -G "Visual Studio 17 2022" -A x64 -DENS_BUILD_APP=ON -DFETCHCONTENT_SOURCE_DIR_JUCE="C:/Users/abart/Desktop/Ensoniq KS-32/build-juce/juce-src"`, then `cmake --build build-app --config Release --target TSReborn_Standalone TSReborn_VST3 TSReborn_AppTests`. No warnings from `app/`; `TSReborn_AppTests.exe` prints `app_tests: all passed`.
- [ ] **Step 5: Manual check, by the controller** (with `TSR_SETTINGS_DIR` set to a temp folder): (a) a first start shows the ROM message; choosing `roms/` boots to `U0-0 BABY-GRAND`; (b) a missing wave ROM shows "missing" without a crash; (c) TUNE=+02 on the System page survives close and reopen; (d) closing within 1 s of opening leaves the memory file's timestamp unchanged. Record the results in the task report.
- [ ] **Step 6:** Confirm `build/` and `build-msvc/` without the option still configure and build without JUCE. Add A109 and A116. Commit: `feat(app): TS Reborn Standalone and VST3: two buses, settings, ROM folder, memory file (A109, A116)`.

### Task 5: Parameters, automation and plugin state

**Files:**
- Modify: `app/source/Processor.{h,cpp}`, `app/source/SettingsPage.{h,cpp}`, `app/tests/app_tests.cpp`, `docs/decisions.md` (A111, A117)

**Interfaces:**
- Consumes: `analogRaw`/`analogPosition` (Task 3), `Engine::setProgram`, `setSelect`, `setAnalog`, `Snapshot::program`/`select` (Task 2).
- Produces: `juce::AudioProcessorValueTreeState& TsProcessor::params()` with IDs `volume`, `dataEntry`, `pitchWheel`, `modWheel`, `cvPedal`, `sound`; `getStateInformation`/`setStateInformation` (A117); `copyToSharedMemory`.

- [ ] **Step 1: Write the failing tests** in `app_tests.cpp`, copied from KS-32 Plan 8's with the TS facts:
  - `parameters`: six parameters with those IDs and the names Volume, Data Entry, Pitch Wheel, Mod Wheel, CV Pedal, Sound; `sound` 0-127; the analog defaults give `kAnalogRest`.
  - `state_round_trip_project`: A boots, System then Up twice (`TUNE=+02`), Sounds, MIDI `C0 20`, `volume` 0.5, 500 ms, state. B gets it and runs 5900 ms: row 0 starts `U0-5`; `volume` 0.5 and `machine().analog(Volume) == analogRaw(Volume, 0.5f)`; System shows `TUNE=+02`.
  - `state_saved_during_boot_keeps_start_image`, `state_without_roms_round_trips`, `state_rejects_bad_blobs` (a RAM of 1000 bytes, `version="2"`, 100 random bytes), `project_mode_leaves_shared_file`, `standalone_state_has_no_ram`: as KS-32, with the 524 288-byte RAM and the TS boot time.
  - `sound_follows_panel`: a panel pick of `Soft2` then `tick()`: `sound` reads 1.
- [ ] **Step 2: Run and see them fail.**
- [ ] **Step 3: Implement** as KS-32 Plan 8 Tasks 3 and 4: the APVTS (A111), `processBlock` passing the five analogs and `sound`, the 30 Hz `tick()` following `Snapshot::program`, the memory modes, the start image, the A117 XML (`TSState`, `select` in hex, `<RAM>` base64 in Project mode), and the settings page's Project-mode texts and "Copy to shared memory…".
- [ ] **Step 4:** `TSReborn_AppTests.exe`: `app_tests: all passed`. `ens_tests` and `ens_rt_tests` on both builds: unchanged.
- [ ] **Step 5:** Add A111 and A117. Commit: `feat(app): host parameters, sound follow, plugin state and memory per format (A111, A117)`.

### Task 6: The front-panel editor

**Files:**
- Modify: `app/source/PanelEditor.{h,cpp}` (KS-32's, adapted), `app/tests/app_tests.cpp`, `docs/decisions.md` (A113-A115)

**Interfaces:**
- Consumes: `panelLayout()` (Task 3), `params()` (Task 5), `Snapshot` (Task 2).

- [ ] **Step 1: Copy KS-32's `PanelEditor`** and draw the TS table, scaled uniformly (A112):
  - colours: panel #1c1d20, button #2b2c30 with border #45474c, labels #c9cbcf, display text #7fe8d6 on #050807 (the photo's teal VFD), LED on #ff4a3d / off #4a2522, the decal #8fb3ad;
  - the display: 2 x 40 cells from `snapshot().panel`, the A113 underline and blink;
  - LEDs from `leds` (bit n lit, bit 16 + n flashing) and `seqLed`;
  - buttons post press and release (A114, with the latch); the patch buttons post `PatchSelect`; sliders and wheels drive their parameters with gestures (the pitch wheel springs to 0.5 on release); keys post `Key` with the A115 strike and draw MIDI-held notes pressed (`Snapshot::notes`);
  - with no engine the display shows `romError()`; the gear opens the settings page; every control has a tooltip and an accessible name; Space presses the focused button; "Show display codes" toggles the hex overlay.
- [ ] **Step 2:** In `app_tests.cpp` keep KS-32's editor check: the VST3 loaded through JUCE's host opens its editor at the default size (1546 x 552) without a crash.
- [ ] **Step 3: Build and run the checks** (Task 4 Step 4's commands): `app_tests: all passed`.
- [ ] **Step 4: Manual check, by the controller:** (a) the default window matches the photo's arrangement; (b) resizing keeps the aspect and the labels readable at the minimum; (c) Presets, then Bank 3, then the lower-left soft button selects a preset (row 1 starts `PSET`) and lights LEDs 1 (Presets) and 10 (Bank 3); (d) a mouse press dragged off a button and released outside does not stick; (e) Data Entry dragged to the bottom on the System page does not flood the display or show an error; (f) clicking near the front of a key is louder than near the back; (g) the pitch wheel springs back. Record the results.
- [ ] **Step 5:** Add A113-A115. Commit: `feat(app): TS-10 front-panel editor (A113-A115)`.

### Task 7: Linux, the VST3 in a host, documents and verification

**Files:**
- Create: `docs/tsapp.md` (the user's readme), `docs/verification-plan4.md`
- Modify: `AGENTS.md` (the app and Linux build commands, `ens_rt_tests`, `app_tests`), `README.md` (Status), `docs/verification-plan3.md` ("Left for Plan 4": mark each item closed or still open)

- [ ] **Step 1: Linux.** In WSL Ubuntu, from `/mnt/c/Users/abart/Desktop/Ensoniq TS`: `cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux`, then `build-linux/ens_tests`, `build-linux/ens_rt_tests` and the three `tsemu render` commands: all pass and the four hashes match. Then `cmake -S . -B build-linux-app -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON -DFETCHCONTENT_SOURCE_DIR_JUCE="/mnt/c/Users/abart/Desktop/Ensoniq KS-32/build-juce/juce-src"` and build `TSReborn_Standalone TSReborn_VST3 TSReborn_AppTests`; run the app tests. If a package is missing, stop and report which (installing one is the user's call).
- [ ] **Step 2: The VST3 in a host.** `app_tests` already loads it through JUCE's host. A DAW check is the user's: record it as "user to run".
- [ ] **Step 3: `docs/tsapp.md`**, in KS-32 `docs/ks32app.md`'s shape: building (Windows and Linux, the JUCE source override, AGPLv3); first start and the ROM folder (`roms/`'s layout); settings and memory locations; the parameters and plugin state; the chord latch; the firmware's own behaviours a player meets: about 3.5 s to the play screen and the restored sound at about 5.3 s; a sound change that loads a new effect mutes the output and ignores notes for about 50 ms (Review Focus 5); VOICE-MUTING on the System page; Data Entry never reaches 0. Open items: the disk drive (empty), the TS-12.
- [ ] **Step 4: `docs/verification-plan4.md`**, in `verification-plan3.md`'s shape: suite counts on the three toolchains and the four hashes; `ens_rt_tests`; `app_tests` on Windows and Linux; the manual checks of Tasks 4 and 6 with results; "user to run": an hour of play from the user's MIDI keyboard with no stuck notes or dropouts; a DAW (Cakewalk, FL Studio or what the user has) with two instances on different sounds saved and reopened; automation lanes for Volume and Sound; the Aux bus routed separately.
- [ ] **Step 5:** Commit: `docs: Plan 4 app readme, build commands, verification`.
