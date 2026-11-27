# Sliding Keyboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A panel button slides the whole lower strip (keys, wheels, patch buttons) out and in, and the window
shrinks to the top panel while it is out.

**Architecture:** The layout gains the strip's boundary (`kTopH`) and the toggle button. The Board moves its strip
parts by an offset and releases them; the editor animates that offset on its own 60 Hz timer and changes its size
and aspect once per toggle (approach A). The choice is a `settings.xml` key.

**Tech Stack:** C++17, JUCE 8 (app only), the repo's `tests/check.h` and `app/tests/app_tests.cpp`.

**Spec:** `docs/design/2026-11-27-keyboard-slide-design.md` (approved 2026-11-27).

## Global Constraints

- AGENTS.md: no JUCE under `src/`; the app checks never open a window and never touch the user's settings
  (`TSR_SETTINGS_DIR`); warnings clean at `/W4` and `-Wall -Wextra`; lines up to about 120 columns.
- The strip is everything at design y >= 430 (`kTopH`); the hidden panel is 2576 x 430 design units.
- Default window 1546 x 552 shown; 1546 x 258 hidden (1546 * 430 / 2576, rounded). Minimum width 1030, maximum
  2 x 2576; aspect 2576:920 shown, 2576:430 hidden. A toggle keeps the width.
- Slide about 250 ms, ease-out. Hiding: slide, then resize. Showing: resize, then slide. A click mid-slide reverses
  from where it is.
- Before the strip moves out: every held key and patch button in it released (latched too), the pitch wheel back to
  centre; the mod wheel keeps its value. No input reaches the strip while it moves or is hidden.
- `settings.xml` key `keyboard`, `shown` / `hidden`, default `shown`; a new editor opens in that state with no
  animation.
- The button: beside the gear, the gear's size, a keyboard icon with a chevron (down while shown, up while hidden),
  tooltip "Hide keyboard" / "Show keyboard".
- Decision A139 in `docs/decisions.md`; `// DECISION A139` at the button, the slide and the size limits.

## Review Focus

1. A click during the slide: it reverses from the current offset, and the window ends at the size of the final
   state (Task 2 check `keyboard_toggle_mid_slide`).
2. A model switch while hidden (A137 rebuilds the board): the new board is hidden too (Task 2 check, in
   `editor_keyboard_slide`).
3. The user resizes the window while hidden: the aspect stays 2576:430 (Task 2 check, in `editor_keyboard_slide`).
4. A Shift-latched patch button in the strip: released when the strip goes out (Task 2 check, in
   `editor_keyboard_slide`).
5. The editor closed mid-slide: no crash, the setting holds the target state, and a new editor opens in it at its
   final size (Task 2 check `keyboard_toggle_mid_slide`).

---

### Task 1: The strip's boundary, the toggle control and the setting

**Files:**
- Modify: `src/app/panel_layout.h`, `src/app/panel_layout.cpp`
- Modify: `app/source/AppSettings.h`, `app/source/AppSettings.cpp`
- Test: `tests/test_app_panel_layout.cpp`, `app/tests/app_tests.cpp`

**Interfaces:**
- Produces: `ens::app::kTopH = 430` and `ens::app::kKeyboardId = -5` in `panel_layout.h`; a
  `{ControlKind::Button, kKeyboardId, {2290, 80, 30, 30}, "Keyboard"}` entry in both model tables;
  `bool AppSettings::keyboardShown() const` and `void AppSettings::setKeyboardShown(bool shown)`.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_app_panel_layout.cpp
// DECISION A139: the strip (keys, wheels, patch buttons and their labels) lies below kTopH, everything else above.
TEST(panel_layout_strip_below_top) {
    for (auto m : {ens::ts::Model::Ts10, ens::ts::Model::Ts12}) {
        int toggles = 0;
        for (const auto& c : ens::app::panelLayout(m)) {
            const bool strip = c.kind == ControlKind::Key || c.kind == ControlKind::Wheel ||
                               c.kind == ControlKind::Patch || (c.kind == ControlKind::Label && c.r.y >= kTopH);
            CHECK(strip ? c.r.y >= kTopH : c.r.y + c.r.h <= kTopH);
            toggles += c.kind == ControlKind::Button && c.id == kKeyboardId;
        }
        CHECK_EQ(toggles, 1);
    }
}
```

In `app/tests/app_tests.cpp`, a `settingsKeyboard(dir)` check called beside `settingsModel`: with
`setSettingsDir(dir)`, `AppSettings().keyboardShown()` is true; after `setKeyboardShown(false)` a new
`AppSettings()` reads false and `settings.xml` holds `keyboard="hidden"`; `true` again reads true.

- [ ] **Step 2: Run them to verify they fail**

Run: `build\ens_tests.exe panel_layout_strip` (fails to compile: `kTopH` not defined), then the app build.

- [ ] **Step 3: Implement**

The constants and the table entry as in Interfaces (both tables come from `build`). The setting stored as the
existing keys are (`model`): `"shown"` / `"hidden"`, anything else reads as shown.

- [ ] **Step 4: Run the tests**

Run: `build\ens_tests.exe panel_layout` and the app checks. Expected: pass. The editor check still passes (the new
button is a `ButtonPart` that does nothing yet: `kKeyboardId` must be treated like the gear in `ButtonPart`, never
posted to the engine).

- [ ] **Step 5: Commit**

`feat(app): the keyboard strip's boundary, its toggle control and setting (A139)`

---

### Task 2: The slide, the size and the releases

**Files:**
- Modify: `app/source/PanelEditor.h`, `app/source/PanelEditor.cpp`
- Modify: `docs/decisions.md` (A139), `docs/tsapp.md` (the panel section: the button)
- Test: `app/tests/app_tests.cpp`

**Interfaces:**
- Consumes: Task 1's `kTopH`, `kKeyboardId`, `AppSettings::keyboardShown` / `setKeyboardShown`.
- Produces, public on `PanelEditor`: `void toggleKeyboard()` (the button's click: starts or reverses the slide and
  stores the target), `bool keyboardShown() const` (the target state), `bool sliding() const`.
- Internal to `PanelEditor.cpp`: `Board::setStripOffset(int dy)` (0 = in place, `kDesignH - kTopH` = out; moves the
  strip parts and the strip's painted background, hides the parts at full offset) and `Board::releaseStrip()` (keys,
  patch buttons latched too, the pitch wheel back to centre).

- [ ] **Step 1: Write the failing checks**

`editorKeyboardSlide(dir)` (`editor_keyboard_slide`), after `editor` in `main`, real ROMs, booted processor:
- the editor opens at 1546 x 552, `keyboardShown()`;
- a key held by `mouseDown` on its part (no `mouseUp`), the pitch wheel dragged up (no `mouseUp`), patch button 0
  latched by a Shift-click; `toggleKeyboard()`; timers run (`juce::Timer::callPendingTimersSynchronously()` with
  `Thread::sleep(10)`) until `!sliding()`, at most 1 s;
- then: 1546 x 258, `!keyboardShown()`, the key's part not visible, the engine's note mask empty after a block,
  `pitchWheel` 0.5, `modWheel` unchanged, no patch select held, `AppSettings().keyboardShown()` false;
- `ed->setSize(2000, 900)`: the constrainer gives 2000 x 334 (2000 * 430 / 2576, rounded);
- a model switch (`p.setModel(Ts12)`, timers run): the new board's key parts are not visible and the size holds;
  back to TS-10;
- a new editor opens at 1546 x 258 with no slide (`!sliding()` at once);
- `toggleKeyboard()` and timers until `!sliding()`: 1546 x 552, key parts visible, the setting true.
- `TSR_SNAPSHOT_DIR`: `panel-keyboard-hidden.png` written by `paintEditor` while hidden.

`keyboardToggleMidSlide(dir)` (`keyboard_toggle_mid_slide`): toggle, run timers about 100 ms, toggle again: the
slide reverses (`sliding()`, then ends shown at 1546 x 552); toggle, then destroy the editor at once: no crash,
`AppSettings().keyboardShown()` false, a new editor opens at 1546 x 258; restore shown at the end.

- [ ] **Step 2: Run them to verify they fail**

Run the app build: fails to compile (`toggleKeyboard` not declared).

- [ ] **Step 3: Implement**

- The button: `ButtonPart` with `kKeyboardId` paints the keyboard icon and chevron (down when shown, up when
  hidden), its tooltip follows; a click (and Space) calls `toggleKeyboard()`.
- The slide: `juce::TimedCallback` at 60 Hz in `PanelEditor`; position from elapsed time over 250 ms with
  ease-out `1 - (1 - t)^2`; reversing restarts from the current offset toward the other end over the remaining
  distance's share of 250 ms. Hiding releases the strip first (`releaseStrip`), slides, then sets the hidden size
  and limits; showing sets the full size and limits, then slides. Strip parts take no mouse input while the offset
  is not 0.
- `resized()` scales by width (`getWidth() / kDesignW`), so the board is the same scale in both states.
- Size limits: `setResizeLimits(1030, 1030 * h / kDesignW, 2 * kDesignW, 2 * h)` and the fixed aspect `kDesignW:h`,
  `h` = `kDesignH` or `kTopH`, set at each toggle's resize step and in the constructor from the setting.
- A rebuilt board (A137, the timer) gets the current offset.
- A139 in `docs/decisions.md` (Options: A, B, C from the design; Choice: A and the design's calls; Evidence: the two
  checks; Status) and one paragraph in `docs/tsapp.md`'s panel section.

- [ ] **Step 4: Run the checks**

Run: the MSVC app build and `TSReborn_AppTests.exe`. Expected: `app_tests: all passed`.

- [ ] **Step 5: Commit**

`feat(app): the keyboard strip slides out and in; the window follows (A139)`

---

### Task 3: Verify and report

**Files:**
- Create: `docs/verification-plan14.md`

- [ ] **Step 1:** Core suites on MinGW, MSVC and Linux (`ens_tests`, `ens_rt_tests`): all pass, hashes
  unchanged (boot `ba7e34a4cf23d1e0`, render `23b6485893beb761`).
- [ ] **Step 2:** App checks on MSVC and Linux: `app_tests: all passed`; look at `panel.png` and
  `panel-keyboard-hidden.png` from `TSR_SNAPSHOT_DIR`.
- [ ] **Step 3:** Write `docs/verification-plan14.md` (as Plan 13's: what changed, toolchains table, open, user to
  run: toggle in the Standalone and a DAW, resize in both states, close and reopen).
- [ ] **Step 4:** Commit `docs: Plan 14 verification`.
