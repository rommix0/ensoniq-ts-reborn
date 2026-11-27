# Verification: Plan 9 (HFE disk images)

Branch `plan9-hfe`, 2026-11-26. No written plan: the work item is "add HFE support" from the user's to-do list and
issue #2. Decision T30 (new).

## What changed

- `decodeImage` reads HxC HFE v1 files with ISO/IBM MFM tracks (both CRCs checked); `encodeImage` writes them. HFE v3
  and other encodings are refused by name.
- `Disk` has its own cylinder count (80 by default, up to 255) for HxC "super disks"; the FDC seeks and reads up to
  the disk's last cylinder. Raw, GKH and Giebler files stay 80 cylinders: a larger disk is saved as `.hfe` only.
- The app lists and opens `.hfe` (files up to 16 MB, was 2 MB), saves back as HFE, and sizes restore stand-ins and
  project media from their block counts. `tsemu --disk` and `--save-disk` take `.hfe`.

## Toolchains and suites

| Toolchain | `ens_tests` | `ens_rt_tests` | App checks |
|---|---|---|---|
| MinGW-w64 i686 (`build`) | 426 passed, 0 failed | 7 passed, 0 failed | n/a |
| MSVC x64 (`build-msvc`, `build-app`) | 426 passed, 0 failed | 7 passed, 0 failed | `app_tests: all passed` |
| Linux GCC x64 in WSL (`build-linux`, `build-linux-app`) | 426 passed, 0 failed | 7 passed, 0 failed | `app_tests: all passed` |

- New tests: disk_more_cylinders, image_hfe_roundtrip, image_hfe_refuses_other_kinds, image_hfe_user_files,
  fdc_reads_past_cylinder_80, media_super_disk_stand_in, ts_loads_hfe_instrument (EPIC STRINGS from ORCH1's HFE:
  24 voices from sample RAM); app check hfeDrive. image_format_for_name gained `.hfe`.
- hfeDrive failed first on the app's 2 MB file cap (the 84-track DD files are 2.1 MB); the cap is 16 MB now.
- No `skip:` line from `ens_tests` on MinGW.
- Speed (MSVC `tsemu bench`): realtime x3.05, run while the other two toolchains' suites were running.

## Reference hashes

Unchanged and identical on MinGW i686, MSVC x64 and Linux GCC: boot trace `ba7e34a4cf23d1e0`; renders
`23b6485893beb761`, `0aa23b0ebbdeeec6`, `e9d27e034e29b527`.

## The 255-cylinder super disk

A probe run (not a committed test: the file is outside the image folder) booted an 8 MB TS with the user's
`DSKA0001_strings.hfe` (HD, 255 cylinders, 10 200 blocks): the OS loaded its 7771-block sound into S8 in 52 s of
emulated time, reading 7822 blocks, 4636 past cylinder 79, the last on cylinder 195, and played it.

## User to run

- Open one of the 84-track `.hfe` files and the 255-cylinder super disk in the Standalone; load and play a sound.
- Save an `.hfe` back and load it on an HxC/Gotek drive with a real TS (the encoder's gaps have only been read by
  this emulator).
- Try saving a super disk as `.img`: the message names `.hfe`.

## Open

- `.efe` images and HFE v3 are not read.
- Loading `.syx` files from the plugin's menu (to-do list).
