# Plan 15 verification: the TS-12 logo

2026-11-27, branch `plan15-ts12-logo`. Design `docs/design/2026-11-27-ts12-logo-design.md`, plan
`docs/superpowers/plans/2026-11-27-ts12-logo.md`, decision A140.

## What changed

- The TS-12 shows its own logo, "TS▾12", in the TS-10 logo's place; switching the model on an open editor switches
  the logo. The Ensoniq badge, the line under the logo and the app icon are unchanged.
- Both logos are rebuilt from the traced TS-10 logo, smoothed: straight lines where the trace runs straight, fitted
  curves elsewhere, the S as straight edges joined by smooth bends, and outline rings at an even gap and width. The
  TS-12 logo shares the T, S, triangle and 1 and has a 2 redrawn to the TS-12 manual cover's. The user approved the
  pictures on 2026-11-27, after three rounds (the cover's coarse 2; the trace's bumps; an angled S).
- A whole-branch review (fresh reviewer) found no critical issues and one important one, fixed: the logos were
  fitted by their ink, so both filled the rect's width and the TS-12 logo was drawn about 4 % larger than the TS-10's;
  they are now fitted by their viewBox, one scale for both.

## Toolchains

| | Core (`ens_tests`) | Real-time (`ens_rt_tests`) | App checks |
|---|---|---|---|
| MinGW-w64 i686 | 442 passed | 8 passed | - |
| MSVC x64 | 442 passed | 8 passed | all passed |
| Linux GCC x64 (WSL) | 442 passed | 8 passed | all passed |

Reference hashes unchanged: boot trace `ba7e34a4cf23d1e0`, render `23b6485893beb761`. The offscreen snapshots
(`TSR_SNAPSHOT_DIR`) `panel.png` and `panel-ts12.png` show the two logos as approved, drawn by JUCE's SVG parser;
`panel-keyboard-hidden.png` shows the TS-10 logo in the top panel.

New and changed checks, the new ones seen failing first: `panel_layout_logo_follows_model` (core: no TS-12 logo
before); `panel_layout_decals_are_clear` (now both models); `panel_layout_ts12_keys` (the logo is the one label that
differs between the tables); `editorTs12HasKeys` (app: the TS-12 logo drew as text with no triangle before; now 343
triangle pixels; after a switch to the TS-10 on the open editor 1216 of the last digit's 1404 letter pixels differ,
and back to the TS-12 none differ; the triangle is 23 x 28 px at y 111 on both models, 25 x 30 against 25 x 28
before the review fix).

## Open (deferred minors from the review)

- The A122 comment above the decals in `src/app/panel_layout.cpp` still says "the TS-10 logo"; the decal is the
  model's logo now (the A140 comment under it says so).
- The two waits for the board's rebuild in `editorTs12HasKeys` do not check the key count was reached; a rebuild that
  never came would fail as a logo mismatch rather than by name.

## User to run

- Switch the model in the Standalone's settings and see the logo change, both ways.
- Look at both logos in a large window (the panel scales up to twice its design size).
