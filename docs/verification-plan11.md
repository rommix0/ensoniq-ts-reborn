# Verification: Plan 11 (the build guide)

Branch `plan11-build-guide`, 2026-11-26. No written plan: the work item is issue #3, a step-by-step build tutorial
for someone who has Visual Studio and a clone but cannot get further. Decision A126 (updated).

## What changed

- `docs/building.md`: the guide. Windows from installing Visual Studio and Git to running `TS Reborn.exe` and
  installing the VST3, through `tools\dist.ps1`; updating; a table of the errors a beginner meets; why Visual
  Studio's "Open Folder" builds only the core. Short Linux (`tools/dist.sh`) and macOS sections.
- README's "Building" section points to the guide first and says that its commands build the core only.
- `tools\dist.ps1` asks `vswhere` for the newest Visual Studio with the C++ tools and names that generator,
  instead of always `Visual Studio 17 2022`. With none, it stops with `No Visual Studio with C++ found`.
- `app/CMakeLists.txt`: the JUCE download uses `core.longpaths=true` (in that clone only, not the user's git
  settings).

## How the guide was checked

A fresh `git clone` of the branch, then the guide's command `powershell -ExecutionPolicy Bypass -File
tools\dist.ps1`, unchanged, JUCE downloaded (no `-JuceSource`):

1. Without `-G`, the first attempt, CMake chose Ninja (this machine's CMake is a MinGW build, whose default is
   Ninja) and stopped at `-A x64`. Hence the explicit `vswhere` choice. It chose Visual Studio 2022 Build Tools
   (17.14) over the Visual Studio 2019 also installed.
2. In the scratchpad folder (about 105 characters deep) the JUCE clone stopped with `Filename too long` on
   JUCE's iOS demo images. A `subst` drive did not help: git writes through the real path. Hence
   `core.longpaths`.
3. With both, from the same deep folder: exit 0 in about 2 minutes. `dist\TS Reborn-1.0.0` holds the exe, the
   VST3 package, the docs and the six ROMs (right sizes) beside the exe and inside the package.
4. `TSReborn_AppTests` with `TSR_RELEASE_VST3` set to that package: `app_tests: all passed` (the package boots
   and plays on its own ROMs).

The 16 apt package names in the Linux section exist in Ubuntu 24.04 (`apt-cache show`); they are the ones the
Linux builds here use. The macOS section uses the commands from `docs/tsapp.md`; not run here (no Mac).

No code under `src/` or `app/source/` changed: the suites and the reference hashes are as in Plan 10.

## User to run

- Follow the Windows guide on a PC without the build tools, or ask the reporter of issue #3 to.
- Visual Studio 2026, if available: the script should name `Visual Studio 18 2026`. Not tested (not installed
  here), so the guide says only that a newer one "should also work".

## Open

- A Visual Studio newer than the CMake on PATH (VS 2026 needs CMake 4.2 or later) fails at configure. Developer
  PowerShell adds Visual Studio's own CMake to PATH, which is why the guide uses it, but after any CMake already
  on PATH (here `C:\winlibs\mingw32\bin` came first). A beginner with only Visual Studio installed has just its
  own.
