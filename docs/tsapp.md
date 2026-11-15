# TS Reborn app: the emulated TS-10 with its front panel

TS Reborn runs the user's TS-10 OS ROM (v3.10) on the emulated machine of Plan 3 (`docs/ts-hardware-interface.md`) and draws the instrument's front panel around it: the 2 x 40 display, the LEDs, every button, the Volume and Data Entry sliders, the two wheels, the two patch buttons and the 61 keys. The firmware drives the display and the LEDs and reads the controls, so every screen is the firmware's own. The app plays from a MIDI keyboard and from the on-screen keys.

It is built in two formats from the same code: a Standalone program (`TS Reborn.exe`) and a VST3 instrument (`TS Reborn.vst3`). Windows x64 (MSVC) and Linux x64 (GCC).

The design is `docs/design/2026-11-13-ts-emulator-design.md`, the decisions are `A100` to `A120` in `docs/decisions.md` (`# App`), and the verification report is `docs/verification-plan4.md`.

## Building

The app is behind the CMake option `ENS_BUILD_APP` (default OFF), so the default builds (`build/`, `build-msvc/`) never need JUCE.

```powershell
cmake -S . -B build-app -G "Visual Studio 17 2022" -A x64 -DENS_BUILD_APP=ON
cmake --build build-app --config Release --target TSReborn_Standalone TSReborn_VST3 TSReborn_AppTests
```

On Linux (JUCE needs the usual X11, ALSA, freetype and fontconfig development packages):

```bash
cmake -S . -B build-linux-app -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON
cmake --build build-linux-app --target TSReborn_Standalone TSReborn_VST3 TSReborn_AppTests
```

- The first configure fetches JUCE 8.0.16-security (git, about 150 MB) into `build-app/_deps`. To use a JUCE checkout you already have, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=<path to juce-src>`; nothing is fetched then.
- `ENS_ROM_DIR` (default: the repository's `roms` folder) is used only by the checks in `TSReborn_AppTests`. The app has no compiled-in ROM path.
- Outputs, Windows: `build-app\app\TSReborn_artefacts\Release\Standalone\TS Reborn.exe` and `...\VST3\TS Reborn.vst3`. Linux: `build-linux-app/app/TSReborn_artefacts/Release/Standalone/TS Reborn` and `.../VST3/TS Reborn.vst3`. The build does not install the VST3. Copy the `TS Reborn.vst3` folder to `C:\Program Files\Common Files\VST3` (Linux: `~/.vst3`).
- Checks: `build-app\app\TSReborn_AppTests_artefacts\Release\TSReborn_AppTests.exe` (Linux: `build-linux-app/app/TSReborn_AppTests_artefacts/Release/TSReborn_AppTests`). They run without a window. They cover the ROM folder and the memory file, the engine's two buses, the host parameters, the state and the memory rules, the disk drive (open, save, write protect, through temp files), and the editor painted offscreen, and they load the built VST3 through JUCE's VST3 host. Without the ROMs the firmware parts skip.

**JUCE licence.** JUCE is dual-licensed: AGPLv3, or a commercial JUCE licence. A build of this app links JUCE, so giving the app to anyone else is under the AGPLv3's terms unless you hold a commercial JUCE licence. Building and using it yourself needs neither.

## First start and the ROMs

The app needs six files in one folder, laid out as the repository's `roms` folder is:

| File | What it is |
|---|---|
| `os/ts10_310h.bin` (256 KB), `os/ts10_310l.bin` (512 KB; 256 KB is accepted) | OS v3.10 |
| `waves/901401_u41.bin`, `waves/901402_u45.bin` (2 MB each), `waves/901301_u43.bin`, `waves/901302_u46.bin` (1 MB each) | The wave ROMs |

If no folder has been chosen and a folder named `roms` sits beside the app, the app takes it and stores it in the settings, so the VST3 finds it too.

Otherwise the first start shows a message in place of the display. Click the gear at the top right of the panel, then **Choose folder...** in ROMS. The list marks each file found, missing or the wrong size. When all six are good the machine starts. The firmware boots in real time, as the instrument does (next section). If a file is missing or the wrong size, the display area names it (for example `ROM missing:` / `os/ts10_310h.bin`). With the ROMs good but no audio device open, it reads `No audio device`. Choosing another good folder later restarts the machine and keeps its memory.

## Where the settings and the memory live

The paths are Windows'. On Linux, `%APPDATA%\TS Reborn` is `~/.config/TS Reborn`.

- **`%APPDATA%\TS Reborn\settings.xml`**: the ROM folder and the memory file. Each change is saved at once. The Standalone and the VST3 share it.
- **`%APPDATA%\TS Reborn\ts-memory.bin`** (the default; **Change...** on the settings page picks another file): the battery memory, 512 KB (the OS RAM and the sequencer expansion RAM, `Machine::ramImage`, T13), so the firmware's settings, edited sounds and sequences survive a restart. It is loaded at start. A file of the wrong size is reported on the settings page and ignored, the machine starts cold, and the first save (on close or after 60 s, once booted) replaces it with the running memory. It is saved on close and every 60 s, but only once the machine has booted (A116). Each save writes `<file>.tmp` and then replaces the file, so a crash in the middle of a save keeps the old image. **Change...** refuses a file in the ROM folder (`ts10_310l.bin` is 512 KB too) and one that cannot be written. A save that fails is reported on the settings page.
- **Erase memory...** asks first, then writes 512 KB of zeros to the memory file and restarts the machine from it. If the file cannot be written, nothing is erased and the settings page says so.
- **The Standalone's own audio-device file** is JUCE's, not ours: `%APPDATA%\TS Reborn\TS Reborn.settings` on Windows (Linux `~/.config/TS Reborn.settings`), with the audio device, buffer size and MIDI inputs. It also holds the wrapper's saved plugin state (the sound and the controls). `TSR_SETTINGS_DIR` does not move it.
- **The VST3 does not write the shared memory file by itself.** A VST3 instance keeps its memory in the host's project (below) and never saves to the file on close or on a timer, so many instances cannot overwrite each other. A new instance starts from the file. **Copy to shared memory...** is the one way a VST3 writes it.

## Audio and MIDI

- **Standalone.** The settings page has JUCE's device selector: output device, buffer size and MIDI inputs. The wrapper's own Options menu also stays. The Standalone's default layout has the Aux bus off, so Main and Aux play together on one stereo pair.
- **VST3.** The host owns the devices. MIDI comes from the host's track, every byte as the MIDI IN jack would receive it, sysex and real-time included (A108). The firmware itself takes Poly-Key and channel pressure from MIDI. One host block carries at most 1024 bytes into the machine (A101): a sysex message over about 1 KB arriving in one block is cut, and the excess is counted (`Snapshot::midiDropped`) but not shown.
- **Outputs (A109).** Two stereo buses: Main and Aux. If the host turns Aux off, Aux is added into Main. When the host bypasses the plugin the machine keeps running and takes MIDI (so a note-off sent while bypassed is not lost), and the output is silent. The Aux outputs carry a track only when its Effects Bus Routing (Track Effects page) is `-AUX-`; the default routing sends everything to Main.
- **MIDI out** from the firmware is counted and not sent anywhere. The app reads it only to know which sound is selected.

## The VST3: the state in the project

The VST3 saves its state with the host's project, so a saved project brings each TS Reborn back as it was (A117). What is saved: the six parameters below, the sound to select again, and the instance's whole 512 KB RAM (about 700 KB in the project). It is the running RAM once the machine has booted. Before the boot ends, it is the RAM the machine started from, so a project saved in the first seconds does not lose its memory.

When a project loads, the instance restarts from the project's RAM. Once the boot has ended, the app selects the saved sound and sets the analogs. A new instance, one not loaded from a project, starts from the shared memory file.

| | Standalone | VST3 |
|---|---|---|
| Start | from the memory file | from the project's RAM if the project has one, else from the memory file |
| Saves to the memory file on close and every 60 s | yes (A116) | **no** |
| Sound and the controls | from JUCE's own wrapper state; the RAM is not in it | from the project |
| **Erase memory...** | zeros to the file, then a cold boot (Sound goes to 0) | this instance only; the file is left alone |
| **Copy to shared memory...** | not shown | asks first, then writes this instance's RAM to the shared file (once booted) |

**The six parameters** (A111): Volume, Data Entry, Pitch Wheel, Mod Wheel and CV Pedal (0 to 1, the position of the control as drawn) and Sound (a whole number, 0 to 127). A drag on a slider or wheel is one host gesture, so the host records automation. The pitch wheel springs back to centre. The Sound parameter follows the sound picked on the panel or by MIDI. With a Sound lane in read mode, the lane overrides panel picks, as automation does in any host.

**Data Entry never reaches raw 0** (A110). The slider's bottom is raw 1, because at raw 0 the OS rewrites the selected value and shows SYSTEM ERROR 144. A host lane at 0 is mapped the same way.

**Which sounds come back** (A105). The app replays the last selection it saw (bank select and program change) once the boot has ended. The firmware always powers on to U0-0, so the display shows U0-0 for the boot and then changes. The rest of the memory (edited sounds, sequences, the system settings) is kept.

## Using the panel

- **Buttons.** A press is sent on mouse-down and its release on mouse-up, wherever the mouse is (A114). Space presses the focused button while held. Every control has a tooltip.
- **The chord latch** (A114). Right-click or Shift-click latches a button down, drawn pressed, until it is clicked again. That is how two buttons are held with one mouse. When the app stops being the foreground program (Alt-Tab), every button held by the mouse is released. Latched buttons stay down.
- **Sliders and wheels** drag vertically. The pitch wheel springs back to centre on mouse-up. The mod wheel stays where it is left.
- **Patch buttons.** The two buttons beside the wheels are the TS's patch selects (sent to the machine as the IEXP lines).
- **Keys.** Click near the front of a key for loud and near the back for soft (A115: strike 1 to 127 along the key, which the OS reads through its own TOUCH table). Dragging across the keys plays each key entered and releases the one left. Notes arriving on MIDI light their keys the same way.
- **Display.** Codes 0x20 to 0x7E are drawn as ASCII, others blank (A113). Underlined cells get a bar. Flashing cells and LEDs blink at 2 Hz. Right-click the display for **Show display codes**, a hex overlay of every cell.
- **The disk drive** (A119, A120). Click the drive on the panel for its menu: **Open Disk Image...**, **New Blank Disk (DD)**, **New Blank Disk (HD)**, **Save Disk Image**, **Save Disk Image As...**, **Eject** and a **Write Protected** tick. A disk image dropped on the panel opens too. The slot shows the file's name, with "(modified)" once the OS has written to the disk since it was opened or saved; a new disk is "Untitled (blank)" until saved. Images: raw `.img` (819 200 bytes DD, 1 638 400 HD; the size decides the density, other sizes are refused), `.gkh`, and Giebler `.ede`/`.eda`. Opening puts the disk in without resetting the TS; the OS sees a disk change, as with a real drive.
  - **Save** writes the file the disk came from, in its format, through `<file>.tmp` and then a replace, so a failed save keeps the old file. A new disk or a read-only file can only be saved with **Save As...**, whose extension picks the format (`.img`, `.gkh`, `.ede`/`.eda`; anything else is raw). Saving takes a moment: the disk is copied between audio blocks and written by the 30 Hz timer, so it needs the audio running.
  - **Write Protected** (off at start) applies to the disk in the drive and to each disk put in: the OS then cannot write, as with the tab on a real disk.
  - Open, New, Eject, a dropped file and closing the Standalone ask first (Save, Don't Save, Cancel) when the disk has unsaved writes. The VST3 cannot ask when a project closes or the plugin is removed: unsaved writes are lost, and "(modified)" is the warning.
  - The drive starts empty: the disk is not remembered across a restart of the app, and the VST3's project state does not carry it. A ROM-folder change or Erase keeps the disk in the drive.
- **Size.** The window opens at 1546 x 552 and resizes from 1030 x 368 up, keeping the panel's shape (A112).

## Firmware behaviours to know

These are OS v3.10's own behaviour. The app does not work around them, except where stated.

- **The boot takes real time.** The play screen appears at about 3.5 s, on U0-0, and the OS plays notes from then on. At about 5.3 s (A104) the saved sound is restored (A105); that program change mutes the output and makes the OS ignore notes for about 50 ms (next item).
- **A sound change that loads a new effect mutes the output and makes the OS ignore notes for about 50 ms.** This shows after a program change, including the restore at the end of the boot. A note played within 50 ms of the change is lost. The app does not delay or resend notes.
- **VOICE-MUTING** is a setting on the OS's System page. It is the firmware's own and the app passes it through unchanged. Its effect has not been checked here.
- **The OS sometimes answers a MIDI-in program change on MIDI out.** The cause is unknown (Low confidence). The app counts a program change that changes the sound and ignores the rest.

## Debug hooks

Environment variables read when the app starts. Neither is needed for normal use.

- **`TSR_SETTINGS_DIR=<folder>`** uses that folder instead of `%APPDATA%\TS Reborn` for `settings.xml` and the default memory file. The checks use it so they never touch your own settings and memory. It does not move the Standalone wrapper's `TS Reborn.settings`.
- **`TSR_SNAPSHOT_DIR=<folder>`** (checks only): `TSReborn_AppTests` writes PNGs of the painted editor there.

## Open items

- **SCSI** is absent.
- **The TS-12** is out of scope for this project; this is the TS-10 only.
- **The OS's answer to a MIDI-in program change** (above) is unexplained.
- **Large sysex.** A sysex message over about 1 KB arriving in one host block is cut (A101). The dropped bytes are counted in the engine but shown nowhere.
- **Levels and the filters** have not been checked by ear (`docs/verification-plan3.md`, "User to run").
- **Label readability** at the minimum window size and the small print the photo has (jack labels, "Bypass", "Replace w/FX") are not drawn. The full list of what a person still has to check is in `docs/verification-plan4.md`.
