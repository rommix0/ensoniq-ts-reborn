# TS Reborn release folder (Plan 6)

Approved by the user on 2026-11-15.

## Intent

A ready-to-use TS Reborn for Windows x64 and Linux x64: one folder holding the Standalone, the VST3 and the
ROMs the app needs, so it runs on another computer with no build tools, no setup and no ROM hunt. The
user's decisions (2026-11-15):

- The release includes the ROMs.
- Only the ROMs the program uses: the six files `app/source/RomFolder.cpp` loads (OS v3.10
  `os/ts10_310h.bin`, `os/ts10_310l.bin`; `waves/901401_u41.bin`, `901301_u43.bin`, `901402_u45.bin`,
  `901302_u46.bin`). Other OS versions, the TS-12 files and `roms/misc/` stay out.
- The ROMs go beside the Standalone and also inside the VST3, so the VST3 works wherever it is installed.
- The logos stay built into the program (A122, already done): the release has no separate logo files.
- The person-only checks of Plans 4 and 5 have been done by the user.

## Goals

- `tools/dist.ps1` (Windows, MSVC x64) and `tools/dist.sh` (Linux, GCC x64, run in WSL on this machine) each
  build the Standalone and the VST3 in Release in a build folder of their own and assemble the release folder.
- The VST3 finds the ROMs inside its own package when no other ROM folder is set.
- The version becomes 1.0.0.

## Non-goals

- No installer, no zip, no code signing, no upload or publishing. Nothing is pushed anywhere.
- No change to how the emulation runs. The reference hashes stay as they are.
- The ROMs are not built into the program: the app still loads them from files (AGENTS.md rule).

## The release folder

```
dist/TS Reborn-1.0.0/              (Linux: dist/TS Reborn-1.0.0-linux/)
  TS Reborn.exe                    (Linux: TS Reborn)
  roms/os/ts10_310h.bin, ts10_310l.bin
  roms/waves/901401_u41.bin, 901301_u43.bin, 901402_u45.bin, 901302_u46.bin
  TS Reborn.vst3/                  the VST3 package
    Contents/x86_64-win/TS Reborn.vst3      (Linux: Contents/x86_64-linux/TS Reborn.so)
    Contents/Resources/roms/...    the same six files
  README.md, LICENSE, APP-GUIDE.md (docs/tsapp.md)
```

The scripts copy the six files from the repo's `roms/` and stop with an error if one is missing or has the
wrong size. `dist/` is already git-ignored.

## Builds

- Windows: Visual Studio 2022 x64, Release, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, so no Visual C++
  redistributable is needed. Build folder `build-dist`.
- Linux: Ninja, Release, `-static-libstdc++ -static-libgcc` for the executable and the plugin. Build folder
  `build-dist-linux`.
- JUCE is needed to build, not to run: it is compiled into the program and the VST3, so the release folder
  needs nothing installed. The scripts let CMake download it (`app/CMakeLists.txt`'s FetchContent, JUCE
  8.0.16-security, into the build folder's `_deps`), unless a `-JuceSource` / `--juce-source` argument
  names a local JUCE checkout. Nothing depends on the KS-32 Reborn folder, which the user will delete.
- The existing `build-app` and `build-linux-app` take JUCE from that KS-32 folder today
  (`FETCHCONTENT_SOURCE_DIR_JUCE`). Plan 6 reconfigures both without it, so they download JUCE into their
  own `_deps` and keep building after the folder is gone; the download is checked by building them.
- The scripts follow KS-32 Reborn's `tools/dist.ps1` and `tools/dist.sh` (KS-32 Plan 8).

## Finding the ROMs (app change)

At start the app takes the first of these that holds the six ROMs:

1. The folder stored in `settings.xml` (chosen in Settings, or stored by 2).
2. A `roms` folder beside the program (as today), stored in the settings so the other format finds it too.
3. Inside the VST3 package: `Contents/Resources/roms`, reached from the plugin binary
   (`currentExecutableFile`, two folders up, then `Resources/roms`). Used, not stored: the package can be
   moved or removed, and the Standalone must not depend on it.

Today a stored folder that has gone missing is an error even when a `roms` folder sits beside the program;
with this order it falls through to 2 and 3. The choice is a decision entry (A125). A ROM folder chosen in
Settings still wins.

## Docs and records

- `AGENTS.md`: the release commands (it says Plan 6 adds them).
- `README.md`: status (Plan 6, version 1.0.0), the release folder, and that a release includes the six ROMs.
- `docs/tsapp.md`: the ROM folder order and the release folder.
- `docs/decisions.md`: A125 (finding the ROMs), A126 (the release folder: contents, runtime linking, JUCE
  source).
- `docs/verification-plan4.md` and `docs/verification-plan5.md`: the "user to run" lists marked as checked
  by the user on 2026-11-15.
- `docs/verification-plan6.md`: the toolchains, the hashes, both scripts run, the folder listing, and the
  VST3 from the release folder loaded headless with its own ROMs.

## Checks

- An app check for the lookup order, with temporary folders standing in for the settings folder, the
  program's folder and a VST3 package (the lookup is a function of those three, so no real install is needed).
- Both scripts run on this machine; the folder holds exactly the files above, the ROMs byte-identical to
  `roms/`.
- The release VST3 loaded through JUCE's VST3 host with no ROM folder set (`TSR_SETTINGS_DIR` pointing at
  an empty temp folder): it boots on its packaged ROMs.
- All suites on MinGW, MSVC and Linux; hashes unchanged.

## Left to the builder

The script structure, how the lookup function is split for the check, the wording of the docs.
