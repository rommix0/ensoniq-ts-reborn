# The display in the Ensoniq VFD font (design)

User's choices on 2026-11-27. Decision A141, updating A113.

## Intent

The panel's display draws its characters in Consolas (A113). The real TS-10 shows two rows of 40 slanted 14-segment
characters on a Futaba VFD (`datasheets/ts10-photo.webp`). tymcode's Ensoniq VFD font is drawn from that display
glass, and its author said it may be used here (the user's to-do list). The display should use it.

Non-goals: no glow, no new glyphs for codes 0x00-0x1F and 0x7F (still blank, with the hex overlay, A113), no change
to the blink rate or the display's place and size.

## Approaches

1. **The font's Regular (Ideal) weight, drawn as a font** (chosen by the user, "cleaner than Organic").
2. The Organic weight: the author's pick for emulators; the user found it less clean. Drawing it heavier
   (an outline traced at draw time) was tried in mock-ups and not taken.
3. Drawing the segments from the package's 14-segment table (`vfd_glyphs.h`): the Ideal look without a font file,
   but it re-implements what the font already draws.

## The design, as decided

- **Font:** `EnsoniqVFD.otf` ("Ensoniq VFD Regular", version 2.000, 114 KB, SIL OFL 1.1) from
  `misc/ensoniq-vfd-font-main/fonts/` (tymcode/ensoniq-vfd-font), copied unchanged to `app/resources/fonts/` with its
  `OFL.txt`, and compiled into the `TSReborn_Art` binary data.
- **Characters:** codes 0x20-0x7E, one per cell of `kDisplayText`. The font is sized so a glyph's advance (720 of
  1000 em) is the cell's width; the glyphs keep their shape (no stretch). The glyph ink (0.058-0.818 em above the
  baseline) is centred in its row. The no-ROMs message uses the font too.
- **Cursor:** an underlined cell draws a rounded bar under the glyph where the font's Cursor weight puts its
  under-cell bar: x 0.053-0.509 em from the cell's left, 0.086-0.164 em below the baseline. A113's blink is unchanged.
- **Hex overlay:** unchanged (the monospaced font), so the codes stay readable.
- **License:** the font's OFL text is in the repo beside the font and in each release folder (`tools/dist.ps1`,
  `tools/dist.sh`); the README credits the font. The OTF carries no license text of its own.
- **Records:** A141 in `docs/decisions.md` (Options as above; the mock-ups as evidence); A113 points to it;
  `docs/tsapp.md` mentions the font where it describes the display.

## Left to the builder

- How the glyph is drawn so it is never dropped for being wider than its cell (A113's `drawText` drops such glyphs):
  a baseline draw at the cell's left is fine.
- The checks: the font loads and is "Ensoniq VFD"; its size gives a glyph advance equal to the cell's width; the
  painted display still lights its cells; an underlined cell lights pixels below the glyph's baseline.
