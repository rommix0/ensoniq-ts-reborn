# Plan 13 verification: issue #9 (General MIDI names, TS-12 buttons, the Standalone's sampled sounds)

2026-11-27, branch `plan13-issue9`.

## What changed

- **T2:** the OS ROM window is 1 MB: the l file's upper half (the General MIDI sounds) at 0xC80000. General MIDI
  mode showed `GM-PROG=001 ( -` and a control code; it now shows `GRAND-PIANO`.
- **T32:** the TS-12 OS reads every panel button two codes above the TS-10's (0x02-0x41). Before, each TS-12 button
  opened the page of the button two codes below it, and the engine's auto-load and prompt answers pressed the wrong
  buttons.
- **A138:** the Standalone records the disk and CD blocks the OS reads and restores its sampled sounds, sound and
  controls at start from the state JUCE's wrapper saves on close (A121 superseded). Power cycle on the settings page
  gives a clean power-on. A whole-branch review found four problems in this part; all are fixed with checks.

## Toolchains

| | Core (`ens_tests`) | Real-time (`ens_rt_tests`) | App checks |
|---|---|---|---|
| MinGW-w64 i686 | 440 passed | 8 passed | - |
| MSVC x64 | 440 passed | 8 passed | all passed |
| Linux GCC x64 (WSL) | 440 passed | 8 passed | all passed |

Reference hashes unchanged on all three: boot trace `ba7e34a4cf23d1e0`, render `23b6485893beb761`.

New tests and checks, each seen failing without its change: `ts_general_midi_names_its_sounds`,
`ts_os_image_interleaves_lower_half` (updated), `ts_panel_ts12_button_codes`, `ts12_buttons_open_their_pages`,
`standalone_restores_state`, `standalone_restores_media` (including the review's no-ROMs Erase case).

## Open

- A no-ROMs start, Erase, then a ROM folder boots the erased memory to `R4-9`, not `U0-0` as `tsemu` does with a zero
  image. Not caused by this branch (it happens without any state); not looked into.
- The Standalone's record has no cap (A129 (k)): CD sectors browsed in one session stay in `TS Reborn.settings` until
  the next clean restore trims them.

## User to run

Done by the user on 2026-11-27 and approved.

- Enter General MIDI mode (MIDI Control, GMIDI, Up, *YES*) and step GM-PROG: the names read as the manual's map.
- On a TS-12: click around the panel (System, MIDI Control, the soft buttons, Tuning): each opens its own page.
- Load a sampled sound in the Standalone, close it, move the image away, start it: the sound plays with no prompt.
  Then Power cycle: the OS asks AUTO-LOAD ALL SAMPLED-SOUNDS.
