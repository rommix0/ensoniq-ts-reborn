# Changelog

What changed in each release of TS Reborn. Issue numbers are on
[GitHub](https://github.com/rommix0/ensoniq-ts-reborn/issues); the letter-and-number ids (A137, T31) are entries in
[docs/decisions.md](docs/decisions.md).

## 1.1.0 (2026-11-27)

### Added

- **The TS-12.** A second model, chosen on the settings page: 76 keys, channel pressure, and its own OS v3.10 pair,
  which ships with the other ROMs. Each model keeps its own memory and ROM folder, and the panel shows the TS-12
  logo when it runs. (T31, T32, A137, A140)
- **macOS.** The Standalone builds on macOS, arm64 and x64, with its icon. Contributed by tymcode (pull request #8).
  (A132)
- **MIDI out.** The OS's MIDI out reaches the VST3's MIDI output and the Standalone's MIDI output device, so editors
  and librarians get its replies. (issue #7, A133)
- **Long sysex.** Sysex messages up to 512 KB go into MIDI IN whole. (issue #7, A101)
- **8 MB sample RAM**, on in the app. (issues #2 and #5, T4, A135)
- **HFE disk images**, and super disks of up to 255 cylinders. (T30)
- **.syx files** go into MIDI IN from the drive menu or by dropping them on the panel. (A136)
- **Sliding keyboard.** A toggle slides the on-screen keyboard out and in; the window follows, and the setting is
  kept. (A139)
- **Power cycle.** On the settings page: restarts the instrument as if switched off and on, keeping its memory.
  (issue #9, A138)
- **The Standalone keeps its sampled sounds and controls** between runs. (issue #9, A138)
- **The display in the Ensoniq VFD font** (SIL Open Font License, credited in the README). The app's own messages
  are in capitals, as the OS's are. (A141)
- **A step-by-step build guide**, [docs/building.md](docs/building.md). (issue #3)
- **Program details.** The author, copyright, project page and contact e-mail are in the program's version
  information and the VST3's vendor fields. (A142)

### Fixed

- No more constant DC at the outputs when the instrument is idle: the outputs are AC-coupled, as the real ones are.
  (issue #6, A134)
- General MIDI names its sounds: the upper half of the OS's low ROM was mapped at the wrong address. (issue #9, T2)
- The TS-12's panel buttons do what they say. (T32)
- Moving the mod wheel no longer crackles on patches whose effect it controls, such as WHL-DDL-BEL. (T33, E34)
- Choosing a new memory file in the Standalone keeps the media it has loaded; choosing an existing file still clears
  them. (A138)
- Holding Space on the keyboard toggle hides or shows the keyboard once, not once per key repeat. (A139)
- The release scripts pick the newest Visual Studio, and the JUCE download works in deep folders. (issue #3)

## 1.0.0 (2026-11-16)

The first release: the TS-10 running its own OS v3.10 in a Standalone app and a VST3 for Windows x64 and Linux x64,
with the front panel drawn and playable, the floppy drive, SCSI and CD-ROM, and the eight ROMs in the release
folder.
