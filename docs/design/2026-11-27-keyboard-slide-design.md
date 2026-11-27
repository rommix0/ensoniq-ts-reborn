# The sliding keyboard: design

2026-11-27. The to-do item "make the keyboard part dockable with a toggle button" (for laptops with lower
resolutions). The user's decisions: a button slides the keyboard in and out with a simple animation; the whole
lower strip goes (keys, pitch and mod wheels, patch buttons); approach A below. The square-variant mockup is set
aside.

Revised 2026-11-27 after the user's check of the first build: the strip slides up under the top panel (it slid down,
out of the window), and the hidden panel is cut at y 390, just under its lowest label (430 left an empty band).

## Intent

The panel is 2576 x 920 design units, 1546 x 552 pixels by default, aspect fixed (A112). On a laptop it is too tall.
With the keyboard strip hidden the window is only the top panel, so it fits; the player uses a MIDI keyboard (with
its own wheels) or the panel alone.

## Goals

- A toggle button on the panel. Hiding: the strip slides down out of view, then the window shrinks to the top
  panel. Showing: the window grows back, then the strip slides up.
- The same in the Standalone and the VST3.
- The choice is remembered.

## Non-goals

- A separate keyboard window (approach C), a second top-panel layout with the wheels, the square variant.
- Any change to what the machine sees, apart from the releases below.

## Anti-goals

- A note or wheel left held when its control disappears.
- A window that stutters in a DAW (approach B's per-frame host resizes).

## Approaches

- **A (chosen by the user).** Slide inside the window, then one resize: one host resize request each way.
- B. Animate the window size with the strip: smoother in the Standalone, but a host resize per frame stutters in
  many DAWs.
- C. Undock the keyboard into its own window: flexible, a much bigger job, and some DAWs handle extra plugin
  windows badly.

## The design (A)

- **What hides.** Everything below design y 430 (my call): the keys, the wheel box with the pitch and mod wheels,
  the patch buttons and the empty band under the top section's divider line (y 395). The hidden panel is
  2576 x 430, about 1546 x 258 at the default width.
- **The button** (my call): a small button beside the gear (top right, the gear's size), drawn as a keyboard icon
  with a chevron: down when the keyboard shows (it will hide), up when hidden. Tooltip "Hide keyboard" / "Show
  keyboard".
- **The animation** (my call): about 250 ms, ease-out, on the editor's timer. Hiding: the strip's parts and its
  background move down behind the bottom edge of the top section, then the editor sets its height to the hidden
  aspect at the same width. Showing: the height first, then the strip moves up. A click during the animation
  reverses it from where it is.
- **Size limits.** The fixed aspect follows the state: 2576:920 shown, 2576:430 hidden; the minimum width stays
  1030, the maximum 2 x 2576. A toggle keeps the window's width.
- **Releases.** Before the strip moves out, every held key and patch button in it is released and the pitch
  wheel springs back to centre (as A114 does on focus loss). The mod wheel keeps its value. No input reaches the
  strip while it moves.
- **Remembered** (my call): `keyboard` (`shown` / `hidden`) in `settings.xml`, shared by the Standalone and every
  plugin instance: the last toggle wins, and a new editor opens in that state (no animation at open).
- **Model switch.** The rebuilt board (A137) takes the current state.
- **Decision.** A139 in `docs/decisions.md`, `// DECISION A139` at the button, the animation and the size limits.

## Tests

App checks (headless, as `editor`): the editor opens shown at 1546 x 552; a toggle, with the animation run to its
end, gives 1546 x 258 (rounded) and no key part visible; a held key and the bent pitch wheel are released by the
toggle; the setting reads `hidden` and a new editor opens hidden; a toggle back gives 1546 x 552 with the keys
visible. The offscreen snapshot (`TSR_SNAPSHOT_DIR`) writes both states.

## User to run

- Toggle in the Standalone and in a DAW: the slide looks smooth and the window follows; resize the window in both
  states; close and reopen.
