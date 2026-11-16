# Verification: Plan 6 (the release folder)

Plan: `docs/superpowers/plans/2026-11-15-ts-release.md`. Design: `docs/design/2026-11-15-ts-release-design.md`.
Branch `plan6-release`, 2026-11-15. Decisions A125 (finding the ROMs) and A126 (the release folder).

## Toolchains and suites

| Toolchain | `ens_tests` | `ens_rt_tests` | App checks |
|---|---|---|---|
| MinGW-w64 i686 (`build`, `ENS_DISK_DIR` set) | 367 passed, 0 failed | 5 passed, 0 failed (0 allocations) | n/a |
| MSVC x64 (`build-msvc`, `build-app`) | 367 passed, 0 failed | 5 passed, 0 failed | `app_tests: all passed` |
| Linux GCC x64 in WSL (`build-linux`, `build-linux-app`) | 367 passed, 0 failed | 5 passed, 0 failed | `app_tests: all passed` |

The floppy tests that need the user's disk images run in the MinGW build only (`ENS_DISK_DIR`); the other two
print "skip: disk images not found".

## Hashes (unchanged, identical on all three)

| Hash | Value |
|---|---|
| Boot trace | `3bfc7336098a6def` |
| Render `player_smoke.mid` | `3374892bfdc150e6` |
| Render `full_chord.mid` | `524fcc64af78f175` |
| Render `filtered_patch.mid` | `01e3c8622cc8c8be` |

## JUCE without the KS-32 folder

The KS-32 Reborn folder was already gone when the plan started, so `build-app` could not configure. Both app
build folders were configured again without `FETCHCONTENT_SOURCE_DIR_JUCE`, after deleting their
`_deps/juce-build`, `_deps/juce-subbuild` and `vst3_helpers/TSReborn` (whose CMake caches named the old
checkout). CMake fetched JUCE 8.0.16-security into each `_deps/juce-src`, and the app checks pass on both
systems. `build-dist` and `build-dist-linux` fetched their own copies. No CMake cache in the build folders names
the KS-32 folder; `git grep` finds it only in the Plan 4 plan, which is history.

## Finding the ROMs (A125)

`app_tests` `rom_folder_order`, six cases: stored good; stored missing (the folder beside the program, stored);
VST3 package (Contents/Resources/roms, not stored); not a package (a `../Resources/roms` is ignored); only a
bad stored folder (kept, so its problem is shown); beside before the package. It failed to compile before
`findRomFolder` existed. `rom_folder_beside_app` is unchanged and passes.

## The release folders (A126)

`tools\dist.ps1` wrote `dist\TS Reborn-1.0.0`:

```
     16545  APP-GUIDE.md
     34020  LICENSE
      2888  README.md
   8073216  TS Reborn.exe
    262144  roms\os\ts10_310h.bin
    524288  roms\os\ts10_310l.bin
   1048576  roms\waves\901301_u43.bin
   1048576  roms\waves\901302_u46.bin
   2097152  roms\waves\901401_u41.bin
   2097152  roms\waves\901402_u45.bin
      1089  TS Reborn.vst3\Contents\Resources\moduleinfo.json
    262144  TS Reborn.vst3\Contents\Resources\roms\os\ts10_310h.bin
    524288  TS Reborn.vst3\Contents\Resources\roms\os\ts10_310l.bin
   1048576  TS Reborn.vst3\Contents\Resources\roms\waves\901301_u43.bin
   1048576  TS Reborn.vst3\Contents\Resources\roms\waves\901302_u46.bin
   2097152  TS Reborn.vst3\Contents\Resources\roms\waves\901401_u41.bin
   2097152  TS Reborn.vst3\Contents\Resources\roms\waves\901402_u45.bin
   8599040  TS Reborn.vst3\Contents\x86_64-win\TS Reborn.vst3
```

`tools/dist.sh` wrote `dist/TS Reborn-1.0.0-linux` with the same files (`TS Reborn` 14 265 248 bytes,
`Contents/x86_64-linux/TS Reborn.so` 15 370 984 bytes, `moduleinfo.json` 1041 bytes). `moduleinfo.json` is
JUCE's, part of every VST3 package.

- Every ROM copy (six files, two places, both systems) has the SHA-256 of its source in `roms/`.
- Linux: `ldd` lists no `libstdc++` for the program or the plugin (linked statically).
- A ROM folder missing `waves/901302_u46.bin`: both scripts stop before building, naming the file
  (`ROM missing: waves\901302_u46.bin in ...`, `ROM missing: waves/901302_u46.bin in ...`), and the release
  folder written before is untouched (same file times).

## The release VST3 on its own ROMs

`app_tests` with `TSR_RELEASE_VST3` set loads that package through JUCE's VST3 host with a fresh settings folder
naming no ROM folder. Before the script put ROMs in the package (the build's own VST3), the note peak was
0.0000 and the check failed. With the release package: note peak 0.1256 / 0.1373 on Windows and on Linux, the
same as the build's VST3 with the ROM folder set, and the settings still name no ROM folder afterwards.

## Person-only checks

The "User to run" lists of `docs/verification-plan4.md` and `docs/verification-plan5.md` were done by the user
(2026-11-15) and are marked so.

## Final review

A fresh whole-branch review found two Important issues, both fixed:
- With the VST3 on its packaged ROMs, the settings page said "No folder chosen" and marked all six files missing.
  The processor now keeps the folder in use (`romFolder()`, `romFolderStored()`); the page shows it, marked
  "(inside the VST3 package)". The memory-file guard checks the same folder, so a memory file inside the
  package's ROM folder is refused (it was accepted before: `rom_folder_in_use` failed 3 checks, now passes).
- "Needs nothing installed" was false on Linux: the build uses the system's glibc 2.38 or later and its desktop
  libraries. README, `docs/tsapp.md` and A126 now say so.

The release folders were built again after the fixes; the release VST3 check passes on both systems.

## Left open

- Deferred from the review: a relative `-RomFolder` / `--rom-folder` resolves against the repository, not the
  caller's folder; an existing `build-dist` cache skips configure (a later `-JuceSource` is ignored);
  `dist.sh` with no VERSION stops before its message; a Linux release copied through Windows tools may lose the
  exec bit (`chmod +x "TS Reborn"`); a stored ROM folder on a drive that is only unplugged is replaced by a
  `roms` folder beside the program.

- The release has no installer, zip or code signing (design non-goals). Windows may warn about an unsigned
  program the first time it runs.
- `README.md` in the release folder links `docs/...` paths that are not in the folder; the app guide is there
  as `APP-GUIDE.md` (the README says so).
- The open items in `docs/tsapp.md` (SCSI, `.hfe`/`.efe`, the OS's MIDI-out answer, large sysex, the photo's
  small print).

## User to run

- On a computer or account with no `%APPDATA%\TS Reborn` folder (Linux: `~/.config/TS Reborn`): unpack the
  release folder, start `TS Reborn.exe` and play; then copy `TS Reborn.vst3` to
  `C:\Program Files\Common Files\VST3` (Linux: `~/.vst3`), open it in a DAW and play, with no ROM folder chosen.
