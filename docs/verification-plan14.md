# Plan 14 verification: the sliding keyboard

2026-11-27, branch `plan14-keyboard-slide`. Design `docs/design/2026-11-27-keyboard-slide-design.md`, plan
`docs/superpowers/plans/2026-11-27-keyboard-slide.md`, decision A139.

## What changed

- A button beside the gear slides the lower strip (keys, pitch and mod wheels, patch buttons) up under the top panel
  and back. Hiding releases what the strip holds, slides it under in about 250 ms, then shrinks the window to the top
  panel (1546 x 234 at the default width, cut just under the lowest label); showing grows the window, then slides the
  strip down. The user's first check found it sliding down out of the window and an empty band under the hidden
  panel; both changed (strip in its own clipped layer, `kTopH` 430 to 390). The choice is stored in `settings.xml`
  (`keyboard`) and a new editor opens in it.
- `settings.xml` is read again before each change, which closes A137's "last writer wins" limit (a processor's
  settings no longer put back another instance's newer values).
- A whole-branch review (fresh reviewer) found no critical issues and two important ones, both fixed: a show went
  through a second, wrong-sized resize; the toggle's chevron was not repainted on a click. A third, raised from minor:
  the panel was scaled by width only, so a window wider than the panel's shape cut off the keys; it now fits by height
  too.

## Toolchains

| | Core (`ens_tests`) | Real-time (`ens_rt_tests`) | App checks |
|---|---|---|---|
| MinGW-w64 i686 | 441 passed | 8 passed | - |
| MSVC x64 | 441 passed | 8 passed | all passed |
| Linux GCC x64 (WSL) | 441 passed | 8 passed | all passed |

Reference hashes unchanged: boot trace `ba7e34a4cf23d1e0`, render `23b6485893beb761`. The offscreen snapshot
`panel-keyboard-hidden.png` (`TSR_SNAPSHOT_DIR`) shows the top panel only, with the toggle beside the gear;
`panel-keyboard-sliding.png` shows the strip 100 ms into a hide, part-way under the panel.

New checks, each seen failing first: `panel_layout_strip_below_top` (core), `settings_keyboard` (including an
`AppSettings` made before the toggle), `editor_keyboard_slide` (including one resize per show and the fit by height),
`keyboard_toggle_mid_slide`.

## Open (deferred minors from the review)

- After a settings change, a processor's later reads of the ROM folder and memory file see another instance's newer
  values (the reload). Arguably better for the shared memory; not yet written into A137.
- Tab, Space or accessibility can still reach a strip button during the 250 ms slide (the mouse cannot).
- The shown minimum height is 367 (integer division), was 368.

## User to run

- Toggle the keyboard in the Standalone and in a DAW: the slide looks smooth and the window follows in one step;
  click again mid-slide; resize the window in both states; close and reopen (it opens as left).
