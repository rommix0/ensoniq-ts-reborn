# Plan 5 verification

Closing check of Plan 5 (`docs/superpowers/plans/2026-11-14-ts-floppy.md`): a working floppy drive. The
uPD72069 controller, a disk in the drive (index pulse, disk change, density), disk images (raw `.img`,
`.gkh`, Giebler `.ede`/`.eda`), the engine's disk hand-over, and the drive on the panel with its menu, save,
write protect and close prompt. The design is `docs/design/2026-11-14-ts-floppy-design.md`; the guide is
`docs/tsapp.md` ("Disk drive"); the decisions are `T21` to `T24` and `A118` to `A120` in
`docs/decisions.md`. The firmware was run unmodified; reading its disassembly was allowed (AGENTS.md,
2026-11-14) and is cited in the entries. MAME was not consulted. No program with a window was started: every
run below was a console program, and the editor was painted offscreen.

## Test counts

All with the ROMs in `roms/` and `ENS_DISK_DIR` set to the user's disk folder, so every firmware and disk
test ran.

| Toolchain | Command | Result |
|---|---|---|
| MSVC x64, Release | `build-msvc\Release\ens_tests.exe` | **363 passed, 0 failed** |
| MinGW-w64 g++ i686, Release | `build\ens_tests.exe` | **363 passed, 0 failed** |
| Linux GCC 13.3 x64, Release (WSL Ubuntu) | `build-linux/ens_tests` | **363 passed, 0 failed** |

Plan 4 ended at 321; Plan 5 added 42: `test_ts_disk.cpp` 8, `test_ts_fdc.cpp` 15, `test_ts_floppy.cpp` 7,
`test_app_engine.cpp` 9 and 3 in other files (not itemised).

### `ens_rt_tests` (no allocation after the boot)

| Toolchain | Result |
|---|---|
| MSVC x64 | 5 passed; 0 allocations |
| MinGW i686 | 5 passed; 0 allocations |
| Linux GCC x64 | 5 passed; 0 allocations |

Plan 4 had 3. The new ones: `rt_disk_in_the_drive_does_not_allocate` (the machine with a disk, reading and
writing) and `rt_engine_disk_swap_does_not_allocate` (insert, copy-out and eject through `Engine::render`).

### Disk tests with and without the images

`ENS_DISK_DIR` is a compile-time path, so "without" needs its own build. With it set (the three runs above)
nothing skipped: `image_user_files` (the four blank Giebler files, AD-001.img and ED-001.GKH decoded and
re-encoded) and the floppy tests that load an ASR-10 and an EPS instrument, format a disk and save it ran.
A MinGW build configured with `-DENS_DISK_DIR=` (empty; built for this check only and deleted) ran
`ens_tests`: **363 passed, 0 failed** and 8 lines `skip: disk images not found`; the tests that need no user
image (the disk and codec tests on built images, the FDC tests, the empty-drive and eject tests) ran.

### `TSReborn_AppTests` (headless)

| Platform | Result |
|---|---|
| Windows x64 (MSVC), `build-app\app\TSReborn_AppTests_artefacts\Release\TSReborn_AppTests.exe` | `app_tests: all passed`, about 55 s (35 s in Plan 4; the disk checks run the real OS) |
| Linux x64 (GCC), `build-linux-app/app/TSReborn_AppTests_artefacts/Release/TSReborn_AppTests`, `DISPLAY` and `WAYLAND_DISPLAY` unset | `app_tests: all passed` |

The new check group `disk_drive` covers: a raw DD file opens; a wrong size is refused naming both sizes; a
Write Data put into the controller makes the slot read "(modified)"; Save clears it and the file holds the
sector; Write Protected ends the write with ST1 NW (0x02); a read-only file refuses Save; Save As changes
the file; a ROM-folder restart keeps the disk; a save asked for just before a restart is written by the new
engine; a save followed at once by New fails and keeps the old file; the slot paints. Both platforms print
`VST3 note peak: 0.1256 0.1373` and `VST3 editor: 1546 x 552`, as in Plan 4.

## Reference hashes

Printed, not asserted. The same on all three toolchains and equal to `AGENTS.md`:

| Hash | Printed by | MSVC x64 | MinGW i686 | Linux GCC x64 |
|---|---|---|---|---|
| Boot trace (re-printed on each: `ens_tests ts_boot_is_deterministic`) | `ts_boot_is_deterministic` | `3bfc7336098a6def` | `3bfc7336098a6def` | `3bfc7336098a6def` |
| Render | `tsemu render tests/data/player_smoke.mid` | `3374892bfdc150e6` | `3374892bfdc150e6` | `3374892bfdc150e6` |
| Render | `tsemu render tests/data/full_chord.mid` | `524fcc64af78f175` | `524fcc64af78f175` | `524fcc64af78f175` |
| Render | `tsemu render tests/data/filtered_patch.mid` | `01e3c8622cc8c8be` | `01e3c8622cc8c8be` | `01e3c8622cc8c8be` |

**One change on purpose.** The player_smoke render was `4a751109deae8b89` in Plans 3 and 4 and is
`3374892bfdc150e6` since Plan 5 Task 4 (T4). Sample RAM is OTTO bank 3 (BankSet S8 at CPU 0x400000), not bank
2: the OS's socket probe stores 0x400000 as S8's base, and a sampled sound loaded from disk played silence
with bank 2. The change moves one sample of that render by 1 LSB (the OS parks voice 0 on bank 3 words
0-0x20000, which now reads the memory manager's header). The other three hashes did not move. All three
toolchains agree, so a sample-RAM bank error is not hidden behind a matching hash.

## Speed (`tsemu bench`)

| Build | Boot, 5.3 s emulated | Chord loop, 20 s emulated |
|---|---|---|
| MSVC x64 Release, run 1 | host 1.392 s, x3.81 | host 7.865 s, **realtime x2.54** |
| MSVC x64 Release, run 2 | host 1.343 s, x3.95 | host 8.166 s, **realtime x2.45** |

Peak 21789 in both, as in Plan 4. The rule is x2 or more on MSVC and it holds. Plan 4 measured x3.79 to
x3.85 on this loop. The controller ran the bench back to back on MSVC on `main` (x2.31, x2.36) and on this
branch (x2.34, x2.37): no regression from Plan 5. The drop from Plan 4's figures is the host (today's
machine state), not the code. A first run during the Linux build gave x1.93 (contention) and was discarded.

## What was found, by task

- **Codecs (T21).** Giebler layout: 512-byte header, a bitmap of omitted blocks (DD at 0xA0, HD at 0x60), a
  type byte at 0x1FF (EPS 3, TS-DD 7, ASR 0xCB, TS-HD 0xCC), then the stored blocks. The four blank files
  re-encode byte for byte.
- **Controller (T22).** Read, write, format, seek, sense and read ID are modelled, without timing. The OS
  lists DD and HD directories, loads, formats and saves through it.
- **Drive lines (T23).** With a disk, IP0 (DSTAT) pulses low 2 ms every 200 ms; IP1 (DSKCHG) goes high on an
  insert or eject and clears when the head steps. The OS's drive check polls the IPCR at c0ea1c and tests only
  bit 4 (the delta of IP0).
- **Density (T24).** IP2 high means HD; the OS writes 0x88 or 0xC8 to the control register to match, and
  0x36 resets the controller.
- **Loading (Task 4).** An ASR-10 instrument (AD-001 GRND PIANO 1, 3055 blocks, about 18 s emulated) and an
  EPS instrument (ED-001 FLUTE 1, 308 blocks, about 2 s) load into BankSet S8 and play; a blank disk is
  formatted and a saved file read back.
- **Threads (A118).** The audio thread applies a disk swap, a write-protect request and a copy-out at the
  block start; it never frees a disk.
- **App (A119, A120).** Save is asynchronous and needs the audio running; the old file is kept when a save
  fails; the Standalone is a custom JUCE application so that closing it can ask about unsaved writes.

## Left open

- **After an aborted FDC command** the controller is in the result phase, so stray data writes parse as command
  bytes (Task 2 carry-over; the OS does not send them).
- **`.hfe` and `.efe` images** are not read. Only raw, GKH and Giebler.
- **SCSI and the CD-ROM** are absent.
- **Confidence notes.** T23: the index period (200 ms) is a real drive's, but any edge within 0.71 s works;
  DSKCHG clearing on a step is medium (the OS looks only at edges, so "drive stays changed" passes the same
  tests); there is no IP1 edge for an insert into an empty drive tested at the OS level. T24: high for IP2
  meaning density and for the reset, low for 0x88/0xC8 being the rate register (not observable). T21: medium
  for Giebler type bytes other than the four seen.
- **The Standalone's close prompt** needs a re-check on a JUCE upgrade (A120). The wrapper's "Reset to default
  state" and a log-off do not go through it.
- **The OS's MIDI-out answer to a MIDI-in program change, large sysex, label readability:** as in
  `docs/verification-plan4.md`.
- **Deferred minors of the review rounds**, none changing behaviour. Code:
  - Giebler decode accepts trailing bytes (the size is a lower bound); no negative Giebler tests; `encodeImage`
    does not check a GKH header's size.
  - `Disk::copyInto` does not copy the write-protect flag (documented in `engine.h`).
  - FDC: Write Data writes live (a reset mid-sector leaves a half-written sector); C >= 80 reports MA, not ND;
    the command mask ignores MT/MF/SK; Format Track ignores the data rate; Seek or Recalibrate on an empty
    drive leaves no interrupt status (the hashes depend on it); an EOT end reports normal termination (a real
    uPD765 without TC gives ST0 0x40 and ST1 EN); DSKCHG clears on a cylinder change, not on a step pulse.
  - `tsemu --save-disk` without `--disk` fails only at the end; `ts_reads_both_densities` prints duplicate skip
    lines; `SampleVoices` counts every BS=3 CR write; `firstSoundName` returning "" passes silently;
    `name.substr(find_first_not_of(...))` can throw on an all-space name.
  - `Engine::takeRemoved` returning nullptr is ambiguous (the app polls on its timer).
  - A leftover `<file>.tmp` when the final replace fails (the old file is kept).
  - The swap-count check and the give-up-on-insert are shown failing only together; the poll's re-request branch
    is unreachable and untested; a restart with no engine leaves a save pending until the next Open, New or
    Eject; the restart check depends on the order of two `step()` ticks.
  - `setStateInformation` may run off the message thread in some hosts (the A117 exposure, noted in A119).
  - Missing FDC tests: invalid command, odd C/H/N, a command after an error, EOT beyond the sector count.

## User to run

Nothing below has been checked by a person. The drive has run only through console programs and the headless
checks.

**With a real window (Standalone, then a VST3 in a host):**
- Load an instrument from `AD-001.img`: open it from the drive menu, press Storage twice on
  the panel, load GRND PIANO 1 into BankSet 8, select it on the Sounds page, and play it.
- The same with `ED-001.GKH`: load FLUTE 1 and play it.
- Save to a new blank disk: New Blank Disk, format it on the panel, save an instrument to it, Save Disk Image
  As..., close and restart the app, open the file, load the instrument back and play it.
- Write Protected: tick it, try to save on the panel, and the OS stops the save. Untick it and the save works.
- The drive menu, the file dialogs, a disk image dropped on the panel, and the "(modified)" mark in the slot.
- The close prompt: close the Standalone with a modified disk (Save, Don't Save, Cancel), and Open, New and
  Eject with one. In the VST3 the unsaved writes are lost on closing a project (the slot's mark is the only
  warning).

**Still open from Plan 4** (`docs/verification-plan4.md`): the mouse checks of the editor, an hour of play,
a DAW with two instances, automation, the Aux bus, and the levels and filters by ear. The five listening
checks of that report (CRUNCH-ELEC, ZEP-KIT, SUITCASE-74, TRANS-CLAV, JAM-WITH-IT) were passed by the user on
2026-11-14.
