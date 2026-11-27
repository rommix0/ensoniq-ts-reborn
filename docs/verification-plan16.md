# Plan 16 verification: the display in the Ensoniq VFD font

2026-11-27, branch `plan16-vfd-font`. Design `docs/design/2026-11-27-vfd-font-design.md`, plan
`docs/superpowers/plans/2026-11-27-vfd-font.md`, decision A141.

## What changed

- The display draws its characters in tymcode's Ensoniq VFD font, Regular (Ideal) weight (the user's choice over
  Organic, after mock-ups of both and of heavier Organic outlines): one 14-segment glyph per cell, its advance the
  cell's width, its ink centred in the row. The cursor is a rounded bar under the glyph, where the font's Cursor
  weight draws it. Codes outside 0x20-0x7E, the hex overlay and the blink are unchanged (A113).
- The font (`app/resources/fonts/EnsoniqVFD.otf`, 114 KB, unchanged) is compiled in. Its SIL OFL 1.1 text is beside
  it, goes into each release folder as `OFL-EnsoniqVFD.txt` (`tools/dist.ps1`, `tools/dist.sh`), and the README
  credits it.

## Toolchains

| | Core (`ens_tests`) | Real-time (`ens_rt_tests`) | App checks |
|---|---|---|---|
| MinGW-w64 i686 | 442 passed | 8 passed | - |
| MSVC x64 | 442 passed | 8 passed | all passed |
| Linux GCC x64 (WSL) | 442 passed | 8 passed | all passed |

Reference hashes unchanged: boot trace `ba7e34a4cf23d1e0`, render `23b6485893beb761`. The snapshots
(`TSR_SNAPSHOT_DIR`) show the font on Windows and Linux: `panel.png` (U0-3 SPACEBELLS..., the cursor bars under the
layered sound's name), `panel-ts12.png`, `panel-noroms.png` ("ROM missing:" and the file name).

New check, seen failing first (link error, `vfdFont` not defined): `vfdFontCheck` (the typeface is "Ensoniq VFD";
A, W, 0 and _ one cell wide within 1 % for cells of 8.48, 14.125 and 28.25; the cursor bar 101.04, 51.69,
8.95 x 1.53 for a cell of 14.125 at baseline 100, 50).

## Open

- The release scripts were syntax-checked, not run end to end; the next release run should show
  `OFL-EnsoniqVFD.txt` in the folder.
- The app's own messages (no ROMs, no audio device) have lowercase letters, which the font draws from its LCD14-based
  set (the hardware shows capitals only): "ts10" reads a little like "+s10".

## User to run

- Read the display at the default and the largest window size.
- A page with a cursor (a name being edited) and a flashing underline (a layered sound).
