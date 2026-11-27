# Verification: Plan 12 (the TS-12 model)

Branch `plan12-ts12`, 2026-11-26. Design `docs/design/2026-11-26-ts12-design.md`, plan
`docs/superpowers/plans/2026-11-26-ts12-model.md`. Decisions T31 (machine) and A137 (app), with updates to A115,
A116, A117, A125 and A126.

## What changed

- Machine (T31): `ts::Model { Ts10, Ts12 }`. The TS-12 has 76 keys, E1 to G7 (MIDI 28 to 103). The key-up byte is
  the release velocity. Channel pressure is on analog mux Y1 (`Analog::Pressure`). There is no Poly-Key code.
  `tsemu --ts12` for `boot`, `render` and `bench`; `--analog PRESS=<raw>`.
- App (A137): a Model setting on the settings page (TS-10 / TS-12). `settings.xml` keeps the last choice and new
  instances start as it. A project keeps one RAM per model (`<TSState model=>`, `<RAM model=>`); a state's model
  wins for that instance only. The Standalone has `ts-memory.bin` (TS-10) and `ts12-memory.bin` (TS-12). A model
  whose ROMs are missing still takes effect and names the missing files. A switch keeps the project's media record
  but not the restore stand-ins. The TS-12 keyboard draws 76 keys, strike 1 to 120 (A115).
- Eight ROMs ship: the TS-12 OS v3.10 pair is added (A125 checks per model, A126 checks and copies eight files).
- Docs: README, AGENTS.md, `docs/tsapp.md`, the design's section 2, decisions A125, A126, A137.

## Toolchains

The four TS-10 reference hashes are unchanged and are the same on all three toolchains.

| Suite | MinGW i686 (`build`) | MSVC x64 (`build-msvc`, `build-app`) | Linux GCC x64 (WSL, `build-linux`, `build-linux-app`) |
|---|---|---|---|
| `ens_tests` | 437 passed, 0 failed | 437 passed, 0 failed | 437 passed, 0 failed |
| `ens_rt_tests` | 8 passed, 0 failed | 8 passed, 0 failed | 8 passed, 0 failed |
| `TSReborn_AppTests` | not built | `app_tests: all passed` | `app_tests: all passed` (7 disk-image skips, 1 `TSR_RELEASE_VST3` skip) |
| `skip:` lines in `ens_tests` | 0 | 0 | 22, all `skip: disk images not found` |

- No `skip:` line from a TS-12 test on any toolchain (the ROMs are in `roms/`).
- The Linux cores and app were configured earlier with an empty `ENS_DISK_DIR` (the images are on the Windows
  side), so the 22 floppy and CD tests print `skip: disk images not found` there. They run and pass on MinGW and
  MSVC. The Linux app checks skip their disk and CD project checks for the same reason.
- Release folder (final fix wave): `powershell -ExecutionPolicy Bypass -File tools\dist.ps1` wrote
  `dist\TS Reborn-1.0.0` with the eight ROMs in `roms\` beside the exe and in the VST3's `Contents\Resources\roms`.
  `TSReborn_AppTests` on MSVC: `app_tests: all passed` without `TSR_RELEASE_VST3` (one skip, `TSR_RELEASE_VST3 not
  set`), and `app_tests: all passed` with `TSR_RELEASE_VST3` set to that folder's `TS Reborn.vst3` (no skip; the
  package loads on its own ROMs, A125).
- `ens_rt_tests` counts 0 allocations in `Machine::runFrame` and `Engine::render`, for the TS-10 and for the
  TS-12 (`rt_ts12_engine_render_does_not_allocate`).
- 68000 suite (`m68k_suite`): not run (the core is unchanged and Tom Harte's tests are not in `testdata/` here).

## Reference hashes

| Hash | Value | MinGW | MSVC | Linux |
|---|---|---|---|---|
| Boot trace (`boot trace hash`) | `ba7e34a4cf23d1e0` | same | same | same |
| Render `player_smoke.mid` | `23b6485893beb761` | same | same | same |
| Render `full_chord.mid` | `0aa23b0ebbdeeec6` | same | same | same |
| Render `filtered_patch.mid` | `e9d27e034e29b527` | same | same | same |

The TS-12 has no reference hash yet: its output is checked by the tests (boot to the play screen, key range,
release byte, pressure), not by a hash.

## Speed

`build-msvc\Release\tsemu.exe`, run while the Linux builds were using the other cores (so a little pessimistic):

- `bench`: boot x5.38, 20 s chord load x3.52.
- `bench --ts12`: boot x5.40, 20 s chord load x3.48.

Both are above the x2 the project requires.

## Memory cross (T31)

The TS-12 and TS-10 OS images were each booted on the other's factory-fresh memory image (test
`ts12_and_ts10_memory_images_cross`): each OS reached the play screen. Not tried with user data (a RAM the user has
edited), so the Standalone keeps one memory file per model and a project one RAM per model (A137).

## User to run

Only a person can check these; none is claimed as passed.

- Play the TS-12 from a MIDI keyboard: the range E1 to G7, and notes outside it.
- The Touch curves on the TS-12 (the OS reads strike through its own TOUCH table, A115).
- Channel pressure from a keyboard that sends it (the sound should respond), and release velocity.
- Switch models in the Standalone (the settings page) and in a DAW (a VST3 instance): the panel redraws with
  76 keys, the display shows that model's OS, the right memory file is used.
- Reopen a project of each model, and a project saved before the TS-12 (it is a TS-10 project).
- A model whose ROM files are missing: the settings page names them.

## Open

- No on-screen control for channel pressure (MIDI channel pressure and `tsemu --analog PRESS=<raw>` only).
- The panel still shows the TS-10 logo on a TS-12 (the logo item decides, A122).
- Other TS-12 OS versions than v3.10 are not supported.
- The memory cross was not tried with user data.
- The TS-12 panel look is not checked against a TS-12 photo (A137 Confidence: Medium).
