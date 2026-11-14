# Plan 4 verification

Closing check of Plan 4 (`docs/superpowers/plans/2026-11-14-ts-juce-app.md`): the real-time engine, the
JUCE app (Standalone and VST3 "TS Reborn") with its two output buses, settings, ROM folder, memory file,
host parameters, plugin state and front-panel editor. It covers the suites on the three toolchains, the
reference hashes, the speed, what the app checks cover, what was checked by hand, and what is left for a
person.

The readme is `docs/tsapp.md`; the decisions are `A100` to `A117` in `docs/decisions.md` (`# App`). The
firmware was run, never studied. MAME was not consulted. No program with a window was started for this
report: every run below was a console program, and the editor was painted offscreen.

## Test counts

All with the ROMs in `roms/` (the default `ENS_ROM_DIR`), so every firmware test runs and none skips.

| Toolchain | Command | Result |
|---|---|---|
| MSVC x64, Release | `build-msvc\Release\ens_tests.exe` | **321 passed, 0 failed**; 0 warnings (`/W4`) |
| MinGW-w64 g++ i686, Release | `build\ens_tests.exe` | **321 passed, 0 failed**; 0 warnings (`-Wall -Wextra`) |
| Linux GCC 13.3 x64, Release (WSL Ubuntu) | `build-linux/ens_tests` | **321 passed, 0 failed** |

Plan 3 ended at 291. Plan 4 added 30: 24 in `tests/test_app_engine.cpp` and 6 in
`tests/test_app_panel_layout.cpp` (which also holds the analog map tests).

### `ens_rt_tests` (no allocation after the boot)

| Toolchain | Result |
|---|---|
| MSVC x64 | 3 passed; `rt_machine_runframe_does_not_allocate` and `rt_engine_render_does_not_allocate` count 0 allocations (225 826 frames) |
| MinGW i686 | 3 passed; 0 allocations |
| Linux GCC x64 | 3 passed; 0 allocations |

### `TSReborn_AppTests` (headless)

| Platform | Result |
|---|---|
| Windows x64 (MSVC), `build-app\app\TSReborn_AppTests_artefacts\Release\TSReborn_AppTests.exe` | `app_tests: all passed`, about 35 s |
| Linux x64 (GCC), `build-linux-app/app/TSReborn_AppTests_artefacts/Release/TSReborn_AppTests`, run with `DISPLAY` and `WAYLAND_DISPLAY` unset | `app_tests: all passed` |

Both print the same figures: `aux bus: mismatches 0, Main peak 0.0001, Aux peak 0.2207 0.0406`,
`key peak back 0.0193, front 0.1276`, `VST3 note peak: 0.1256 0.1373`, `VST3 editor: 1546 x 552`,
`state: 699406 bytes`, `editor row 1: PSET BOREALISPAD  SITAR-PAD  VELO-ORIENT`,
`data entry restore: [R2-2  BLEND-KEYS  PNO-N-VOX  PNO-N-CHOIR], select b000002002c00e`,
`data entry on TUNE: [SYSTEM  TUNE=-96          PITCH-BEND=02 ]`, `zero-length and bypassed blocks: note 60 held 0`.

What the checks cover: the ROM folder states; the memory file (load, atomic save, Erase, wrong size); the
processor's guards; the two buses and the Aux-off mix into Main (a track routed to `-AUX-` through the
running OS, so Aux has sound); the six parameters; the state round trip, during the boot, without ROMs, with
bad blobs, from other threads and before the Standalone's boot; the sound follow races; the bank restore
(R2-2); a project with Data Entry away from rest restored as left; MIDI in a 0-sample block and in a
host-bypassed block; the editor driven by synthetic mouse events and painted to an image; and the built VST3 loaded
through JUCE's VST3 host (name, instrument, two output buses, a note after the boot, the editor created
without a window).

The VST3 in a host is therefore checked here with JUCE's own host, headless. A DAW is the user's (below).

## Linux build

WSL Ubuntu, GCC 13.3, CMake and Ninja. `build-linux` (core) and `build-linux-app` (app, with
`-DFETCHCONTENT_SOURCE_DIR_JUCE` pointing at the KS-32 checkout's `juce-src`; nothing was downloaded) built
with no missing package. JUCE's configure reported libcurl and webkit2gtk as not found; the app does not
use them.

The Linux app build uses stricter flags than `-Wall -Wextra` (`-Wfloat-equal`, `-Wswitch-enum`,
`-Wsign-conversion`). It first printed warnings from our own files; they were fixed without changing
behaviour (`PanelEditor.cpp`, `app_tests.cpp`, `src/otto/otto.h`). After the fix the Linux core and app
builds print 0 warnings, and so do the MinGW, MSVC and MSVC app builds. `ens_tests` was re-run on all three:
321 passed, and the boot and render hashes are unchanged. `TSReborn_AppTests` passes on Windows and Linux
again.

## Reference hashes

Printed, not asserted. The same on all three toolchains and equal to `AGENTS.md`:

| Hash | Printed by | MSVC x64 | MinGW i686 | Linux GCC x64 |
|---|---|---|---|---|
| Boot trace | `ts_boot_is_deterministic` | `3bfc7336098a6def` | `3bfc7336098a6def` | `3bfc7336098a6def` |
| Render | `ts_render_is_deterministic`; `tsemu render tests/data/player_smoke.mid` | `4a751109deae8b89` | `4a751109deae8b89` | `4a751109deae8b89` |
| Render | `tsemu render tests/data/full_chord.mid` | `524fcc64af78f175` | `524fcc64af78f175` | `524fcc64af78f175` |
| Render | `tsemu render tests/data/filtered_patch.mid` | `01e3c8622cc8c8be` | `01e3c8622cc8c8be` | `01e3c8622cc8c8be` |

Plan 4 changed no emulation output. `src/ts` gained `Machine::seqLed()`, `analog()`, `panelRoom()` and
`panelDropped()`, which only read state.

## Speed (`tsemu bench`)

| Build | Boot, 5.3 s emulated | Chord loop, 20 s emulated |
|---|---|---|
| MSVC x64 Release, run 1 | host 0.908 s, x5.84 | host 5.273 s, **realtime x3.79** |
| MSVC x64 Release, run 2 | host 0.907 s, x5.84 | host 5.197 s, **realtime x3.85** |
| MinGW i686 Release | host 0.949 s, x5.59 | host 5.463 s, realtime x3.66 |

Peak 21789 in every run. The rule is x2 or more on MSVC.

## Checked by hand in this plan

Only what could be run without a window being driven by a person:

- **Task 4, Standalone start (controller, temporary `TSR_SETTINGS_DIR`).** With the ROM folder set, the
  Standalone booted to `U0-0 BABY-GRAND` (read from a window capture, made before the headless-only rule).
  Closing within 1 s left the memory file's time and hash unchanged. A 9 s run wrote a 524 288-byte
  `ts-memory.bin` on close.
- **Task 6, the editor.** Painted offscreen by `TSReborn_AppTests` (set `TSR_SNAPSHOT_DIR=<folder>` to keep
  the PNGs). The arrangement matches `datasheets/ts10-photo.webp`: disk drive at the left; Volume, Up/Down
  and Data Entry; the display with three soft buttons above and below; the mode and bank rows with their
  LEDs; the Track Parameters, sequencer and Programming blocks; the decal and gear; the wheel box with the
  patch buttons; the 61 keys. Presets, Bank 3 and Soft 4 clicked through the parts' mouse handlers give row 1
  `PSET BOREALISPAD ...` with LEDs 1 and 10 lit. The pitch wheel returns to exactly 0.5. A key clicked at the
  front peaks at 0.1276 on Main and at the back at 0.0193.
- The photo's small print is not drawn: jack labels, "Bypass", "Replace w/FX", "Group Edit", "Layer",
  "Effects", and the maker's logo (left out on purpose).

## Findings worth keeping

- **The OS sometimes answers a MIDI-in program change on MIDI out.** Unexplained, Low confidence. The
  engine's tracker counts only a program change that changes the program, so the answer does not move the
  host's Sound parameter (A105). Open.
- **The Aux outputs carry a track only when its Effects Bus Routing (Track Effects page) is `-AUX-`.**
  Found by pressing buttons on the running OS. The default routing sends everything to Main.
- **After a program change that loads a new effect, the OS mutes and ignores notes for about 50 ms**
  (Musician's Manual, "Switching Effects in Song Mode"). The restore at the end of the boot happens once
  (`engine_restore_happens_once_at_boot_end`). A note played in the first 50 ms after a change is lost.
- **Timings:** the play screen at about 3.5 s, the restored sound at about 5.3 s.
- **Data Entry never reaches raw 0** (A110, `analog_map_data_entry_never_zero`).
- **The Standalone keeps its own audio-device file** (`TS Reborn.settings` beside `settings.xml`); the
  environment variable `TSR_SETTINGS_DIR` does not redirect it.

## Final review fixes

The whole-branch review of Plan 4 (`041c239`) found these; each check was written first and failed
before the fix (Data Entry excepted: it passed, so the code stayed).

- **A 0-sample block with Aux off dropped its MIDI** (A109). JUCE's VST3 wrapper calls `processBlock` with
  0 samples when the host passes outputs, and events can arrive then; the Aux-off loop never ran, so a
  note-off was lost and the note stayed held. Now a 0-sample block is one piece of 0 frames: its messages
  go in at offset 0 and the engine renders 0 frames. Check `zeroAndBypassedBlocks`: `90 3C 64` in a normal
  block sets note 60 in `Snapshot::notes`, `80 3C 00` in a 0-sample block clears it (before: still held).
- **Host bypass dropped MIDI** (A109). `processBlockBypassed` was JUCE's default, so a note-off sent while
  bypassed was lost. It now runs `processBlock` (the machine keeps running) and clears the buffer. Same
  check: a note-off through `processBlockBypassed` clears note 60 and the block is silent (before: held).
- **Data Entry at the boot's end** (A106) was untested. A project's Data Entry away from rest reaches the
  machine at the boot's end, a slider move to the OS. Check `dataEntryRestored`: a state on R2-2 with
  TUNE=+02 and Data Entry 0.9 comes back after the boot and 500 ms on R2-2 with the saved select and
  TUNE=+02. It passed as the code stood. The same slider moved to 0.1 on the TUNE page then sets TUNE=-96,
  so the input is live and the check can fail. No `src/` change; the hashes are untouched.
- Docs: the large-sysex limit is marked open (below and in `verification-plan3.md`); `docs/tsapp.md` gives
  the boot's real note behaviour, the sysex limit, the bypass, and the wrong-size memory file being
  replaced at the first save (also A116); the README is in the present tense; the app checks' run time is
  one figure here and in `AGENTS.md`.

## Plan 3's "Left for Plan 4"

Marked closed or open in `docs/verification-plan3.md`. Closed: the engine and its allocation test, Data
Entry's floor, the battery RAM in the app's state and file, the SQX-70 (not offered), the threading
contract. Open: MIDI-in bursts over 1024 bytes in one block (dropped and counted, the count shown
nowhere), the disk and SCSI, the empty SEQEXP socket's read, display
attributes `E7` and `7F`, SACK/, wait states, the CPU clock, the levels.

## Left open

- The OS's MIDI-out answer to a MIDI-in program change.
- MIDI in over 1024 bytes in one host block: the engine keeps 1024 bytes a block and drops the rest,
  counted in `Snapshot::midiDropped` but shown nowhere, so a sysex message over about 1 KB arriving in one
  block is cut silently. Pacing it or showing the count is for a later plan.
- The disk drive (empty) and the TS-12.
- Deferred minors of the review rounds, none changing behaviour. Code: the editor test does not check
  begin/end gestures; no automated check of underline, blink, latch, focus release, patch buttons or the
  reset on an engine change; `paramId` falls back to Data Entry for an unknown analog; closing the editor can
  lose a release if the event queue is full; the audio thread's `getStateInformation` can wait up to 300 ms
  on a concurrent save; `tick` and `copyToSharedMemory` read the engine without `stateMu_` (as KS-32); an
  oversize block with Aux off is split in the processor as well as in the engine; a leftover `roms/` beside the test program fails
  `romsBesideApp`; several lines over 120 columns (`PanelEditor.cpp`, `Processor.cpp`, `SettingsPage.cpp`,
  one in `ts-hardware-interface.md`); a debug `printf` in the bus test. Tests: NaN and infinity in
  `analogRaw` are untested; the LED-to-button tolerance lets adjacent bank LEDs swap; the Erase check cannot
  tell zeros from the old file.
- From the final review, deferred: the VST3 looks for `roms/` beside the host's program and would store
  that folder in the shared settings (limit it to the Standalone, store only a good folder); in Project
  mode, "Change…" on the memory file restarts the instance without asking; saving a project waits up to
  300 ms per instance when the host is not running audio; `setStateInformation` stores the start image
  before a restart that may fail; the tracker's program-change dedup (A105) has no note of its limit; the
  RT test has no oversize render or Aux-off path; `feq` in the app tests treats NaN as equal.

## User to run

Nothing below has been checked by a person.

**Mouse checks of Task 4 (Standalone):**
- First start with no ROM folder: the panel shows the ROM message, the gear opens the settings page, and
  Choose folder... then starts the machine.
- A folder with a file missing: the display area names the missing file.
- TUNE (System page) kept across close and reopen.

**Mouse checks of Task 6 (the editor):**
- The default window matches the photo's arrangement; resizing keeps the aspect; the labels stay readable at
  1030 x 368.
- A press dragged off a button and released outside does not stick.
- Alt-Tab while holding a button releases it; a latched button stays down.
- Right-click or Shift-click latches a button; Space presses the focused button.
- Data Entry dragged to its bottom on the System page does not flood the display or show an error.
- Clicking near the front of a key sounds louder than near the back (by ear).
- The pitch wheel springs back to centre.
- Presets, then Bank 3, then the lower-left soft button: row 1 starts `PSET` and LEDs 1 and 10 light, with a
  real mouse.
- The tooltips, and a screen reader reading the control names.

**Play and DAW:**
- An hour of play from a MIDI keyboard: no stuck notes, no dropouts.
- A DAW (Cakewalk, FL Studio or what you have): two instances on different sounds, the project saved and
  reopened; each comes back with its sound and memory.
- Automation lanes for Volume and Sound.
- The Aux bus routed to a separate track (set a track's Effects Bus Routing to `-AUX-` first).
- Listening: levels, the high-pass filter (OT9) and filter routing (OT10), as in `verification-plan3.md`.
- The same suites on your own Linux machine, if it is not WSL.
