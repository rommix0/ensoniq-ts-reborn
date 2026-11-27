# Verification: Plan 8 (core fixes from issues #2-#7)

Branch `plan8-core-fixes`, 2026-11-26. No written plan: the work list is the user's to-do list and GitHub issues
#2-#7. Decisions A101 (updated), A133, A134, A135, T4 (updated). The TS-12 is back in scope (the user's decision).

## What changed

- **Issue #7, long sysex.** MIDI-in bytes are held (512 KB) and fed while DUART A's wire queue has room, so a sysex
  longer than 1024 bytes crosses blocks at the wire's pace instead of being cut (A101).
- **Issue #7, MIDI out.** The firmware's MIDI out goes to the host as whole messages: the VST3's MIDI output, and the
  Standalone's MIDI output device, now in its settings (A133).
- **Issue #6, DC at idle.** Cause: OT12's exponent-0 volume and T11's `>> 4` truncate a stopped-sounding voice's
  negative filter output to -1 LSB at ESP SER0; the effect carries it to Main (about -2 LSB). It is the same on the
  commit before OT16, so the pitch-wheel fix did not cause it. The chips stay bit-exact; the processor models the
  TS's AC-coupled outputs with a 5 Hz DC blocker (A134).
- **Issues #5 and #2, sample RAM.** `Machine::create(..., simm8)` and `tsemu --simm8` fit the 4M x 8 SIMM pair:
  8 MB, BankSets S8 and S9 (T4). The app always runs 8 MB (A135).
- **Issue #2, two buttons with one mouse.** Already there (the chord latch, A114); the README now says how.

## Toolchains and suites

All three were built with `ENS_DISK_DIR` set to the user's disc folder (`misc/EPS-ASR10 Discs`). The build folders
had been configured at the repo's old path (`C:\Users\abart\Desktop\Ensoniq TS`) and were reconfigured; the JUCE
sources in `build-app` and `build-linux-app` were kept (`FETCHCONTENT_SOURCE_DIR_JUCE`).

| Toolchain | `ens_tests` | `ens_rt_tests` | App checks |
|---|---|---|---|
| MinGW-w64 i686 (`build`) | 419 passed, 0 failed | 7 passed, 0 failed | n/a |
| MSVC x64 (`build-msvc`, `build-app`) | 419 passed, 0 failed | 7 passed, 0 failed | `app_tests: all passed` |
| Linux GCC x64 in WSL (`build-linux`, `build-linux-app`) | 419 passed, 0 failed | 7 passed, 0 failed | `app_tests: all passed` |

- New core tests: engine_long_sysex_is_not_dropped, engine_midi_out_whole_messages, ts_sample_ram_8mb_fills_banks_3_and_2,
  ts_simm8_has_bankset_s9; engine_midi_overflow_is_counted rewritten for the held store. New app checks: midiOut,
  dcBlocked (failed before the fix: Main mean -1.975 / -2.741 LSB; after: 0.000 / 0.000), sampleRam8.
- No `skip:` line was printed by `ens_tests` on Windows.
- Speed (MSVC `tsemu bench`): realtime x3.60.
- `TSR_RELEASE_VST3` was not run (no release folder was built).

## Reference hashes

Unchanged and identical on MinGW i686, MSVC x64 and Linux GCC: boot trace `ba7e34a4cf23d1e0`; renders
`23b6485893beb761` (player_smoke), `0aa23b0ebbdeeec6` (full_chord), `e9d27e034e29b527` (filtered_patch).

## User to run

- A sysex librarian or editor on the Standalone's MIDI output and input: an Identity Request is answered (with
  `SYS-EX=ON`), and a dump sent from the TS (Storage, MIDI SYS-EX) arrives whole.
- A `.syx` bank (TSD-1000 etc.) sent to the VST3 or Standalone with `SYS-EX=ON` loads; at the wire's pace a
  100-300 KB message takes 30-100 s.
- Silence after a note: no DC step or click when sound starts and stops.
- Load a sampled sound into BankSet S9 and play it (bank 2 has not been heard yet; T4).
- The chord latch for a two-button shortcut (hold a Bank button, press a soft button).

## Open

- HFE disk images and loading `.syx` files from the plugin's menu (to-do list).
- The TS-12 (in scope again): what its OS and keyboard add.
