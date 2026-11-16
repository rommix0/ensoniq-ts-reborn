# Verification: Plan 7 (SCSI and CD-ROM)

Plan: `docs/superpowers/plans/2026-11-15-ts-scsi.md`. Design: `docs/design/2026-11-15-ts-scsi-design.md`.
Branch `plan7-scsi`, 2026-11-16. Decisions T25-T29 (machine), A127-A131 (app); T12 and A124 updated.

## Toolchains and suites

All three were built with `ENS_DISK_DIR` set to the user's disc folder (and `build-app`, `build-linux-app` too,
so the app checks get `TSR_DISK_DIR`).

| Toolchain | `ens_tests` | `ens_rt_tests` | App checks |
|---|---|---|---|
| MinGW-w64 i686 (`build`) | 412 passed, 0 failed | 7 passed, 0 failed | n/a |
| MSVC x64 (`build-msvc`, `build-app`) | 412 passed, 0 failed | 7 passed, 0 failed | `app_tests: all passed` |
| Linux GCC x64 in WSL (`build-linux`, `build-linux-app`) | 412 passed, 0 failed | 7 passed, 0 failed | `app_tests: all passed` |

- Real-time suite: 0 allocations on all three. `rt_engine_cd_load_does_not_allocate`: 0 allocations, 6 sectors
  served. `rt_engine_restore_does_not_allocate`: 0 allocations over 158036 frames.
- No `skip:` line was printed by `ens_tests` on any toolchain: the image tests (floppy and CD) ran everywhere.
- `TSR_RELEASE_VST3` (`dist\TS Reborn-1.0.0\TS Reborn.vst3`, built by `tools\dist.ps1`, version unchanged):
  `app_tests: all passed`, VST3 note peak 0.1383 / 0.1320, on its own ROMs. Not repeated on Linux (`dist.sh`
  was not run in this task; Plan 6 checked it and nothing in the packaging changed).

## Hashes (unchanged, identical on all three)

| Hash | Value |
|---|---|
| Boot trace | `3bfc7336098a6def` |
| Render `player_smoke.mid` | `3374892bfdc150e6` |
| Render `full_chord.mid` | `524fcc64af78f175` |
| Render `filtered_patch.mid` | `01e3c8622cc8c8be` |

The default machine has no SCSI board, so the hashes did not move.

## Tests that needed the user's images

These read `ENS_DISK_DIR` and skip without it: `cd_file_reads_user_cds`, `ts_cd_*` (STORAGE and SCSI STATUS pages,
the Load page on a CD, a CD sound loaded and played), the engine and media tests that restore sounds from a
floppy and a CD, and the app checks' project round trip (floppy ED-001 and CD CDR-013; the state holds 314 and
316 blocks, and after a restore and a new save only the blocks the auto-load read). The images were only read.

## User to run

Not claimed as passed.

- Insert a CD image in the Standalone (menu or drag-and-drop of a `.iso` / `.cue`), browse it on the OS's Load
  page and load a sound; eject; try a file chooser and a real drop.
- Save a VST3 project in a DAW with sounds loaded from a floppy and from a CD, move the images away, reopen the
  project and play.

## Left open

Deferred minors from the per-task reviews, grouped.

- Image reading: `CdFile` checks a relative FILE against the cwd before the `.cue` folder; `parseCue` is strict
  (BOM, tabs, keyword case, WAVE); `cd_file_reads_user_cds` skips when open fails with `ENS_DISK_DIR` set; no
  synthetic test of the 16-byte skip; `openCd` skips when an existing image is rejected.
- SCSI target: medium ids must never be reused; READ(10) past the end is not range-checked (answers 3/0x11, a
  real drive 5/0x21); no test crossing a sector boundary; REQUEST SENSE ignores a pending UNIT ATTENTION; MODE
  SENSE WP bit 0; an insert mid-transfer also ends INQUIRY / REQUEST SENSE data with CHECK CONDITION; `releaseCd`
  with a pending eject returns `{medium,0}`.
- WD33C93: soft Reset does not zero registers 0x01-0x16; CIP held during a transfer; group 2 CDB taken as 10
  bytes; `reset()` leaves INT clear; address wraps past 0x1F; count underflow unclamped; no DataOut test;
  `wd_select_timeout` 0x10 assert vacuous; `reg()` has no bounds check.
- Floppy marks: no HD / end-of-track / aborted-sector mark tests; the write case in `fdc_read_marks_blocks` does
  not assert the write ran.
- Restore: the end rule is untested for its 2 s and 120 s limits; an error page ignoring Up gets Up every ~2.1 s
  until the cap; `setRestore` silently no-ops on bad preconditions; `loadRam` after `setRestore` resets the
  auto-load; no CD SKIP test; `captureCd` hard-codes 2048; first capture wins after the user writes the disk.
- App: the A128 read-rate figure is unmeasured (reader `wait(1)` adds ~8-16 ms a sector on Windows; a brief yield
  burst would fix it); `cdDriveMissingFile` lacks the STATUS checks before and after; `~CdDrive` `stopThread(2000)`
  may kill a hung read; `tick` reads the snapshot outside `stateMu_` (a `setState` right after a restore ends
  can finish the new restore); capture lags up to 2 ticks; the audio thread copies up to 30 times a second while
  the OS reads; the state parser is lenient ("12abc", overlapping runs, duplicate media, NUL in a label); no
  restart-during-restore check; the CD trim check proves whole-medium trimming only.
- Style: a ~140-column comment in the Task 4 test, `Audio boot;` shadows `boot()`, `tsemu bench --cd` ignores
  the image, a ~124-column comment at `disk.h:96`, stale comment `media_record.h:4`.
- Carried from earlier plans: the open items in `docs/tsapp.md`.
