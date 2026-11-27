# The TS-12 logo (design)

Approved by the user on 2026-11-27. Decision A140.

## Intent

A TS-12 shows the TS-10 logo today (`docs/tsapp.md`, "the panel still shows the TS-10 logo on a TS-12"). The panel
should show the TS-12's own logo when the TS-12 model runs, in the same design as the TS-10's. Both logos should look
clean: the TS-10 logo, traced from a scan (A122), has bumps the user noticed in the S and the 0 and wobbly outline
rings.

Non-goals: no change to the Ensoniq badge, the line under the logo, the app icon (A132) or any layout place. Dropped
on the way: a "TS-RB" logo replacing both (the user's first idea, then scrapped).

## Approaches

1. **The TS-12 manual cover's vector logo as it is.** `datasheets/TS-12 Musician's Manual.pdf`, page 10, holds the
   logo as vector shapes. Rejected by the user: the 2 is a coarse chain of 33 straight segments and the outline is
   loose.
2. **The TS-10 logo's T, S, triangle and 1, plus a redrawn 2** (chosen). The two logos share every letter but the
   last, so they match exactly. The 2 is drawn with smooth curves to the cover's 2 (proportions, its place by the 1,
   the 1's flag angle for its slanted ends).
3. **Redraw every letter by hand.** More work for no gain over 2 once the traced letters are smoothed.

## The design, as decided

- **Smoothed letters (both logos).** Straight runs of the trace become straight lines, curves become smooth cubics
  fitted to the trace. The S is fitted as straight edges joined by bends whose tangents match (no kinks, no tight
  near-corners), within about 2 units of the trace (mean 0.56) on a 437-unit-high logo. The 0 keeps its egg-shaped
  counter.
- **Outline rings recomputed** for every letter at an even gap (9 units) and width (6), measured from the traced
  rings; rings of neighbouring letters merge where they meet, as before.
- **Colours** as A122 for both models (letters #85A5A2, triangle #8894AF, outline #B4B8C4); there is no TS-12 photo.
- **Files:** `app/resources/ts10_logo.svg` replaced by the smoothed logo (viewBox 1550 x 437), new
  `app/resources/ts12_logo.svg` (1490 x 437), each with the paths `outline`, `letters`, `triangle`.
- **Panel:** the TS-12 table's logo decal is labelled "TS-12" and drawn from `ts12_logo.svg`, in the TS-10 logo's
  rect `{2010, 152, 377, 106}`, kept to its shape and centred (the narrower logo fills the height and is centred
  across). No layout rect changes.
- **Records:** A140 in `docs/decisions.md`; A122 notes the smoothing; AGENTS.md lists the TS-12 logo among the Ensoniq
  files in git (the user's request); `docs/tsapp.md` drops "the panel still shows the TS-10 logo on a TS-12".

## Left to the builder

- How the editor picks the drawable (by the decal's label).
- The checks: `panel_layout_decals_are_clear` for both models; the app's TS-12 editor check finds the triangle in
  the TS-12 logo and a different picture there than the TS-10's.
- The generator scripts (scipy, shapely, PyMuPDF) stay out of the repo: the SVGs are the source now, and A140
  records how they were made.
