# TS Reborn Release Folder Implementation Plan (Plan 6)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `tools/dist.ps1` and `tools/dist.sh` build TS Reborn 1.0.0 and assemble a release folder holding the Standalone, the VST3 (with its own copy of the ROMs) and the six ROMs, and nothing depends on the KS-32 Reborn folder any more.

**Architecture:** The app gains one lookup function, `findRomFolder`, that picks the stored ROM folder, a `roms` folder beside the program, or the ROMs inside the VST3 package, in that order. The two scripts follow KS-32 Reborn's `tools/dist.ps1` / `tools/dist.sh`: configure a Release build folder of their own, build the two targets, copy the artefacts, the six ROMs and the docs into `dist/`. The app checks gain an optional run of the release VST3 with no ROM folder set.

**Tech Stack:** C++17, JUCE 8.0.16-security (CMake FetchContent), CMake 3.20+, PowerShell 5.1, bash (WSL Ubuntu), MSVC x64, GCC x64.

**Spec:** `docs/design/2026-11-15-ts-release-design.md` (approved 2026-11-15). Background: `AGENTS.md`, `docs/tsapp.md`.

## Global Constraints

- The firmware runs unmodified; the app still loads the ROMs from files. The ROMs are not compiled in.
- No JUCE, OS calls or file I/O under `src/`. This plan changes nothing under `src/`.
- The reference hashes stay: boot `3bfc7336098a6def`; renders `3374892bfdc150e6` (player_smoke), `524fcc64af78f175` (full_chord), `01e3c8622cc8c8be` (filtered_patch), identical on MSVC x64, MinGW-w64 i686 and Linux GCC.
- The release holds only the six ROMs the app loads: `os/ts10_310h.bin` (262 144 bytes), `os/ts10_310l.bin` (524 288), `waves/901401_u41.bin` (2 097 152), `waves/901301_u43.bin` (1 048 576), `waves/901402_u45.bin` (2 097 152), `waves/901302_u46.bin` (1 048 576), copied byte for byte from the repo's `roms/`.
- Release folder names: `dist/TS Reborn-1.0.0` (Windows), `dist/TS Reborn-1.0.0-linux` (Linux). `dist/` is git-ignored; nothing under it is committed.
- JUCE: downloaded by CMake (`app/CMakeLists.txt` FetchContent, tag `8.0.16-security`) unless a local checkout is passed in. No file, script or build folder may depend on `C:/Users/abart/Desktop/Ensoniq KS-32` after this plan.
- Windows runtime: `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`. Linux: `-static-libstdc++ -static-libgcc` on executables and modules.
- Tests never touch the user's settings or memory: the app checks set `TSR_SETTINGS_DIR` to a temp folder.
- Never open a window or start `TS Reborn.exe` or a plugin GUI; everything is checked headless.
- Warnings clean at `/W4` and `-Wall -Wextra` in our own files.
- Decision entries: A125 (finding the ROMs), A126 (the release folder), in `docs/decisions.md`'s format, with `// DECISION <id>:` at the point of use.
- Never push. Commit per task on the plan branch `plan6-release`, merged into `main` with a merge commit at the end.

## Review Focus

1. **A stored ROM folder that was deleted or moved** while a `roms` folder sits beside the program: the app starts on the one beside it instead of showing an error. Test: `rom_folder_order` case "stored missing" (Task 1).
2. **The VST3 installed alone** (copied to the system VST3 folder, no settings file): it boots on the ROMs in its package and does not store that folder. Test: the release VST3 check (Tasks 3 and 4).
3. **A program not inside a VST3 package** (the Standalone, the checks) never takes a `../Resources/roms` that happens to exist. Test: `rom_folder_order` case "not a package" (Task 1).
4. **A stored folder that exists but is incomplete**, with nothing else found: the app still names that folder's problem on the settings page, as today. Test: `rom_folder_order` case "only a bad stored folder" (Task 1).
5. **A missing or wrong-size ROM, an old release folder, paths with spaces** ("Ensoniq TS", "TS Reborn"): the script stops before writing anything when a ROM is wrong, replaces an old folder of the same version, and quotes every path. Test: the script run with `-RomFolder` pointing at a temp copy missing one file (Task 3 Step 5, Task 4 Step 3).

---

### Task 1: Finding the ROMs (A125)

**Files:**
- Modify: `app/source/RomFolder.h`, `app/source/RomFolder.cpp`, `app/source/Processor.h`, `app/source/Processor.cpp:74-86` (the constructor's ROM lookup) and `setRomFolder`
- Modify: `docs/decisions.md` (A125), `docs/tsapp.md` (the ROM folder paragraph, line 41)
- Test: `app/tests/app_tests.cpp` (new `romFolderOrder`, called from `main` next to `romsBesideApp`)

**Interfaces:**
- Produces:
  ```cpp
  // RomFolder.h
  struct RomFolderChoice {
      juce::File dir;      // empty: none found
      bool store = false;  // write it to settings.xml
  };
  // The folder the app starts on (A125): `stored` if it holds the six ROMs; else a "roms" folder beside
  // `program`, stored; else, when `program` sits in a VST3 package (its parent's parent is named
  // "Contents"), Contents/Resources/roms, not stored; else `stored` as it is (its problem is reported),
  // not stored.
  RomFolderChoice findRomFolder(const juce::File& stored, const juce::File& program);
  // Processor.h: as today, plus `store` (false: used, not written to the settings)
  bool setRomFolder(const juce::File& dir, juce::String& err, bool store = true);
  ```
  "Holds the six ROMs" means every `checkRomFolder(dir, nullptr)` entry is `Found`.

- [ ] **Step 1: Write the failing check `romFolderOrder(const juce::File& dir)`** using `dummyRoms` (right sizes) and these cases, each a `CHECK` on `findRomFolder`'s result:
  - "stored good": stored = good A, program beside a good B → `{A, false}`.
  - "stored missing": stored = `dir/gone` (never created), program = `dir/app/TS Reborn.exe` with good `dir/app/roms` → `{dir/app/roms, true}`.
  - "package": stored empty, program = `dir/pkg/TS Reborn.vst3/Contents/x86_64-win/TS Reborn.vst3` (create the parent folders; the file itself need not exist) with good `dir/pkg/TS Reborn.vst3/Contents/Resources/roms` → `{that roms folder, false}`.
  - "not a package": stored empty, program = `dir/plain/bin/TS Reborn.exe` with good `dir/plain/Resources/roms` and nothing beside → `{juce::File(), false}`.
  - "only a bad stored folder": stored = `dir/bad` holding only `os/ts10_310h.bin`, nothing else → `{dir/bad, false}`.
  - "beside beats package": stored empty, the package layout above plus good `Contents/x86_64-win/roms` → that beside folder, `store == true`.

- [ ] **Step 2: Build `TSReborn_AppTests` and run it.** Expected: does not compile (`findRomFolder` undeclared).

- [ ] **Step 3: Implement `findRomFolder` in `app/source/RomFolder.cpp`**, then use it in the `TsProcessor` constructor with `settings_.romFolder()` and `juce::File::getSpecialLocation(juce::File::currentExecutableFile)` (the DLL or .so in a plugin, the exe in the Standalone): a non-empty `dir` goes to `setRomFolder(dir, romError_, choice.store)`, else `romError_ = "No ROM folder:\nopen Settings"` as today. `setRomFolder` writes `settings_.setRomFolder(dir)` only when `store`. Mark both with `// DECISION A125:`.

- [ ] **Step 4: Build and run the app checks on MSVC.** `cmake --build build-app --config Release --target TSReborn_Standalone TSReborn_VST3 TSReborn_AppTests`, then `build-app\app\TSReborn_AppTests_artefacts\Release\TSReborn_AppTests.exe`. Expected: `app_tests: all passed`, `romsBesideApp` unchanged and passing, no warnings from `app/`.

- [ ] **Step 5: Write A125 and fix `docs/tsapp.md`'s ROM folder paragraph** (the order, the package case, a missing stored folder falling through). A125's Evidence names `rom_folder_order` and its six cases.

- [ ] **Step 6: Commit.**
  ```bash
  git add app/source/RomFolder.h app/source/RomFolder.cpp app/source/Processor.h app/source/Processor.cpp app/tests/app_tests.cpp docs/decisions.md docs/tsapp.md
  git commit -m "feat(app): find the ROMs beside the program or inside the VST3 package (A125)"
  ```

### Task 2: JUCE without the KS-32 folder

**Files:**
- Modify: `docs/tsapp.md` (build section), `AGENTS.md` (Building)
- Build folders only (not committed): `build-app`, `build-linux-app`

**Interfaces:** none (build setup).

- [ ] **Step 1: Reconfigure `build-app` without the KS-32 JUCE.** `cmake -S . -B build-app -U FETCHCONTENT_SOURCE_DIR_JUCE -DENS_BUILD_APP=ON`. Expected: CMake clones JUCE `8.0.16-security` into `build-app/_deps/juce-src`, and `build-app/CMakeCache.txt` has no `FETCHCONTENT_SOURCE_DIR_JUCE` value naming `Ensoniq KS-32`. If configure fails on the old population, delete `build-app/_deps/juce-*` and configure again.

- [ ] **Step 2: Build and run the app checks there** (as Task 1 Step 4). Expected: `app_tests: all passed`.

- [ ] **Step 3: The same for `build-linux-app` in WSL** (`wsl -d Ubuntu -- bash -lc '...'`, repo under `/mnt/c/Users/abart/Desktop/Ensoniq TS`): `cmake -S . -B build-linux-app -U FETCHCONTENT_SOURCE_DIR_JUCE -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON`, build the three targets, run `build-linux-app/app/TSReborn_AppTests_artefacts/Release/TSReborn_AppTests`. Expected: `app_tests: all passed`.

- [ ] **Step 4: Check nothing still names the KS-32 folder.** `git grep -n "Ensoniq KS-32/build-juce"` lists only the historical `docs/superpowers/plans/2026-11-14-ts-juce-app.md`; `grep FETCHCONTENT_SOURCE_DIR_JUCE build-app/CMakeCache.txt build-linux-app/CMakeCache.txt` shows no KS-32 path.

- [ ] **Step 5: Docs.** `docs/tsapp.md` and `AGENTS.md`: JUCE is downloaded by CMake on the first configure (git and a network connection needed once per build folder); `-DFETCHCONTENT_SOURCE_DIR_JUCE=<checkout>` is optional.

- [ ] **Step 6: Commit.**
  ```bash
  git add docs/tsapp.md AGENTS.md
  git commit -m "docs: JUCE is downloaded by CMake; no build depends on the KS-32 folder"
  ```

### Task 3: The Windows release folder (A126) and version 1.0.0

**Files:**
- Create: `tools/dist.ps1`
- Modify: `app/CMakeLists.txt` (`VERSION 0.4.0` → `1.0.0`, both places), `app/tests/app_tests.cpp` (the release VST3 check), `docs/decisions.md` (A126)

**Interfaces:**
- Consumes: Task 1's lookup (the release VST3 must boot on `Contents/Resources/roms`).
- Produces: `tools/dist.ps1 [-RomFolder <folder>] [-JuceSource <JUCE checkout>]` → `dist\TS Reborn-1.0.0\`; prints `dist: <full path>` last. `-RomFolder` defaults to the repo's `roms`.
- Produces: env `TSR_RELEASE_VST3=<path to a TS Reborn.vst3 package>` makes the app checks run the release VST3 check; unset, it prints `  skip: TSR_RELEASE_VST3 not set`.

- [ ] **Step 1: Write the failing release VST3 check.** Make the existing `vst3` check a function of the bundle and a flag, `void vst3(const juce::File& dir, const juce::File& bundle, bool ownRoms)`. With `ownRoms` the settings folder is a fresh temp folder with no ROM folder set (no `AppSettings().setRomFolder` call). Assertions added for `ownRoms`: the note peak is above 0.001 on both channels after the boot (as today), and `AppSettings().romFolder() == juce::File()` after the plugin is released (the package folder is not stored). `main` calls `vst3(dir/"vst3", TSR_VST3_PATH, false)` and, when `TSR_RELEASE_VST3` is set, `vst3(dir/"release", that path, true)`.

- [ ] **Step 2: Run the check against a package with no ROMs in it.** `$env:TSR_RELEASE_VST3 = "<repo>\build-app\app\TSReborn_artefacts\Release\VST3\TS Reborn.vst3"`, run the app checks. Expected: the release check FAILS (no ROMs found, no sound).

- [ ] **Step 3: Write `tools/dist.ps1`**, following KS-32 Reborn's `tools/dist.ps1` (read it in `C:\Users\abart\Desktop\Ensoniq KS-32\tools\dist.ps1` while that folder exists; the steps are below in any case):
  1. The version from `app/CMakeLists.txt`'s first `VERSION x.y.z`.
  2. Every one of the six ROMs (Global Constraints) checked in `-RomFolder` for presence and exact size before anything is built or written; on a failure, throw naming the file.
  3. If `build-dist\CMakeCache.txt` is missing, configure `build-dist`: Visual Studio 17 2022, x64, `-DENS_BUILD_APP=ON`, `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, plus `-DFETCHCONTENT_SOURCE_DIR_JUCE=<JuceSource>` only when `-JuceSource` is given.
  4. Build `TSReborn_Standalone` and `TSReborn_VST3`, Release.
  5. Remove an old `dist\TS Reborn-<version>`, then copy in: `TS Reborn.exe`, the `TS Reborn.vst3` package, `README.md`, `LICENSE`, `docs\tsapp.md` as `APP-GUIDE.md`; the six ROMs into `roms\` and into `TS Reborn.vst3\Contents\Resources\roms\`, each keeping its `os\` or `waves\` subfolder.
  `$ErrorActionPreference = 'Stop'`; every native command's exit code checked; every path quoted. Mark the script's header comment `DECISION A126`.
  Bump `app/CMakeLists.txt` to `VERSION 1.0.0` (the plugin and the checks).

- [ ] **Step 4: Run it.** `powershell -ExecutionPolicy Bypass -File tools\dist.ps1` (no `-JuceSource`: it downloads JUCE into `build-dist\_deps`). Expected: last line `dist: ...\dist\TS Reborn-1.0.0`; the folder holds exactly `TS Reborn.exe`, `TS Reborn.vst3\` (with `Contents\x86_64-win\TS Reborn.vst3` and the six ROMs under `Contents\Resources\roms`), `roms\` (six files), `README.md`, `LICENSE`, `APP-GUIDE.md`; each ROM copy has the same SHA-256 as its source in `roms\` (`Get-FileHash`).

- [ ] **Step 5: Run it against a broken ROM folder.** Copy the six ROMs to a temp folder, delete `waves\901302_u46.bin` there, run `tools\dist.ps1 -RomFolder <temp>`. Expected: it stops with an error naming `waves/901302_u46.bin` (or `waves\901302_u46.bin`), and `dist\TS Reborn-1.0.0` from Step 4 is untouched (same file times).

- [ ] **Step 6: The release VST3 check passes.** Rebuild the app checks in `build-app`, set `TSR_RELEASE_VST3` to `<repo>\dist\TS Reborn-1.0.0\TS Reborn.vst3`, run them. Expected: `app_tests: all passed` (the Step 2 failure is gone); unset, the check prints its skip line and the rest pass.

- [ ] **Step 7: Write A126** (contents, the six ROMs twice, static runtime, JUCE download or `-JuceSource`, no zip, no installer, no signing) and commit.
  ```bash
  git add tools/dist.ps1 app/CMakeLists.txt app/tests/app_tests.cpp docs/decisions.md
  git commit -m "feat(dist): Windows release folder with the ROMs, version 1.0.0 (A126)"
  ```

### Task 4: The Linux release folder

**Files:**
- Create: `tools/dist.sh` (mode 755 in git: `git update-index --chmod=+x tools/dist.sh`)

**Interfaces:**
- Consumes: Task 3's layout and checks (the same steps, Linux names).
- Produces: `tools/dist.sh [--rom-folder <folder>] [--juce-source <JUCE checkout>]` → `dist/TS Reborn-1.0.0-linux/`; prints `dist: <full path>` last.

- [ ] **Step 1: Write `tools/dist.sh`**: `set -euo pipefail`; the same order as Task 3 Step 3 (version, ROM check first, configure `build-dist-linux` with Ninja, Release, `-DENS_BUILD_APP=ON`, `"-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"`, `"-DCMAKE_MODULE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"`, and `-DFETCHCONTENT_SOURCE_DIR_JUCE` only with `--juce-source`; build both targets; copy `TS Reborn`, the `TS Reborn.vst3` package, the docs and the six ROMs into `roms/` and `TS Reborn.vst3/Contents/Resources/roms/`). The ROM size check uses `stat -c %s`.

- [ ] **Step 2: Run it in WSL.** `wsl -d Ubuntu -- bash -lc 'cd "/mnt/c/Users/abart/Desktop/Ensoniq TS" && tools/dist.sh'`. Expected: last line `dist: .../dist/TS Reborn-1.0.0-linux`; the folder holds `TS Reborn`, `TS Reborn.vst3/Contents/x86_64-linux/TS Reborn.so`, the six ROMs in both places with the same `sha256sum` as `roms/`, and the three docs. `ldd "TS Reborn"` lists no `libstdc++`.

- [ ] **Step 3: Run it with `--rom-folder` at a temp copy missing `waves/901302_u46.bin`.** Expected: it exits non-zero naming the file; the Step 2 folder is untouched.

- [ ] **Step 4: The release VST3 check on Linux.** Build `TSReborn_AppTests` in `build-linux-app`, run it with `TSR_RELEASE_VST3="/mnt/c/.../dist/TS Reborn-1.0.0-linux/TS Reborn.vst3"`. Expected: `app_tests: all passed`.

- [ ] **Step 5: Commit.**
  ```bash
  git add tools/dist.sh
  git update-index --chmod=+x tools/dist.sh
  git commit -m "feat(dist): Linux release folder"
  ```

### Task 5: Docs and verification

**Files:**
- Modify: `AGENTS.md` (Building: the two release commands; replace "The release-folder (Plan 6) commands are added by that plan."), `README.md` (status: Plan 6, version 1.0.0, the release folder, that a release includes the six ROMs), `docs/tsapp.md` (a "Release folder" section: contents, where to put the VST3, the ROMs inside it), `docs/design/2026-11-13-ts-emulator-design.md` section 11 (Plan 6 done)
- Modify: `docs/verification-plan4.md`, `docs/verification-plan5.md` ("User to run": checked by the user on 2026-11-15)
- Create: `docs/verification-plan6.md`

- [ ] **Step 1: Run every suite on the branch head.** MinGW `build\ens_tests.exe`, `build\ens_rt_tests.exe`; MSVC `build-msvc\Release\ens_tests.exe`, `ens_rt_tests.exe`; Linux `build-linux/ens_tests`, `ens_rt_tests`; the three `tsemu render` commands on each. Expected: all pass, 0 allocations, the four hashes of Global Constraints on all three.

- [ ] **Step 2: Write the docs listed above.** `verification-plan6.md`: toolchains and counts, hashes, both scripts' runs and listings, the release VST3 check on both systems, the broken-ROM runs, the JUCE download in `build-app`, `build-linux-app`, `build-dist`, `build-dist-linux`; "User to run": open the release Standalone and the release VST3 (copied to the system VST3 folder) on a machine or account with no `%APPDATA%\TS Reborn` settings.

- [ ] **Step 3: Commit.**
  ```bash
  git add AGENTS.md README.md docs/tsapp.md docs/design/2026-11-13-ts-emulator-design.md docs/verification-plan4.md docs/verification-plan5.md docs/verification-plan6.md
  git commit -m "docs: Plan 6 release folder, verification"
  ```
