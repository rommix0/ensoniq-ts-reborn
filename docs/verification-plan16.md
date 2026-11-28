# Plan 16 verification: the display in the Ensoniq VFD font

2026-11-27, branch `plan16-vfd-font`. Design `docs/design/2026-11-27-vfd-font-design.md`, plan
`docs/superpowers/plans/2026-11-27-vfd-font.md`, decision A141.

## What changed

- The display draws its characters in tymcode's Ensoniq VFD font, Regular (Ideal) weight (the user's choice over
  Organic, after mock-ups of both and of heavier Organic outlines): one 14-segment glyph per cell, its advance the
  cell's width, its ink centred in the row. The cursor is a rounded bar under the glyph, where the font's Cursor
  weight draws it. Codes outside 0x20-0x7E, the hex overlay and the blink are unchanged (A113).
- The font (`app/resources/fonts/EnsoniqVFD.otf`, 114 KB, unchanged) is compiled in. Its SIL OFL 1.1 text is beside
  it, goes into each release folder and the VST3 package's `Contents/Resources` as `OFL-EnsoniqVFD.txt`
  (`tools/dist.ps1`, `tools/dist.sh`), and the README credits it and LCD14 (OFL 1.1, Fredrick Brennan), the set its
  characters beyond the hardware-checked ones follow.
- A whole-branch review (fresh reviewer) found no critical or important issues. Re-graded and fixed: the OFL went
  only into the release folder, not into the VST3 package that is installed on its own; LCD14 was not credited; the
  cursor bar's place inside its row was not checked (and a comment overstated its margin).
- The app's own messages (no ROMs, no audio device) are shown in capitals, as the hardware shows everything (the
  user's request: the font's lowercase made "ts10" read like "+s10"). The ROM list keeps a file's exact name.

## Toolchains

| | Core (`ens_tests`) | Real-time (`ens_rt_tests`) | App checks |
|---|---|---|---|
| MinGW-w64 i686 | 442 passed | 8 passed | - |
| MSVC x64 | 442 passed | 8 passed | all passed |
| Linux GCC x64 (WSL) | 442 passed | 8 passed | all passed |

Reference hashes unchanged: boot trace `ba7e34a4cf23d1e0`, render `23b6485893beb761`. The snapshots
(`TSR_SNAPSHOT_DIR`) show the font on Windows and Linux: `panel.png` (U0-3 SPACEBELLS..., the cursor bars under the
layered sound's name), `panel-ts12.png`, `panel-noroms.png` ("ROM MISSING:" and the file name, in capitals).

New check, seen failing first (link error, `vfdFont` not defined): `vfdFontCheck` (the typeface is "Ensoniq VFD";
A, W, 0 and _ one cell wide within 1 % for cells of 8.48, 14.125 and 28.25; the cursor bar 101.04, 51.69,
8.95 x 1.53 for a cell of 14.125 at baseline 100, 50; on the panel's 14.125 x 29 cell the bar ends at y 26.31, inside
the row; the messages in capitals, row by row).

## Open

- Deferred minors from the review: `vfdFont` falls back only when the typeface is null, not when it loads but
  measures no width (an infinite height; not reachable with the font compiled in unchanged); spaces are shaped and
  drawn like other characters (80 cells per repaint, as the old `drawText`).
- The release scripts were syntax-checked, not run end to end; the next release run should show
  `OFL-EnsoniqVFD.txt` in the folder and in the VST3 package's `Contents/Resources`.

## User to run

- Read the display at the default and the largest window size.
- A page with a cursor (a name being edited) and a flashing underline (a layered sound).
