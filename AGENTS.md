# AGENTS.md

Guidance for coding agents working in this repository. Read [README.md](README.md) first for what the
project is; this file is how to work on it.

## The project in one paragraph

An emulator of the Ensoniq TS-10 (the TS-12 is out of scope) that runs the instrument's own OS v3.10 from the
ROMs in `roms/`. Plain C++17 chip cores (68000, ES5506 OTTO from Plan 2, ES5510 ESP, 68681 DUART)
make up a TS machine (`src/ts`, from Plan 3), a real-time engine drives it, and a JUCE 8 app wraps
that engine as a Standalone and VST3 named "TS Reborn" (`app/`, from Plan 4) for Windows x64 (MSVC),
Linux x64 (GCC) and macOS arm64/x64 (Apple clang). The 68000, ESP and DUART cores were copied from KS-32 Reborn at commit
`c17c35c`; the design is `docs/design/2026-11-13-ts-emulator-design.md`.

## Rules that are not negotiable

- **The firmware runs unmodified, and the app still needs the user's ROMs.** The OS is run as-is in
  emulation; nothing from it is patched, rebuilt or shipped in place of the ROM files. Studying the
  firmware (disassembling the OS, reading the ESP effect programs it loads, tracing them) is allowed
  as a source of evidence since 2026-11-14 (the user's decision); cite what was found in the decision
  entry, as for any other evidence.
- **MAME is a comparison point, never a reference.** Do not copy MAME's behaviour to match it.
  `docs/reference/mame/` (git-ignored) holds the user's working copy of MAME's TS driver for
  comparison only; keep it out of the code.
- **No JUCE, OS calls or file I/O under `src/`.** The cores, the machine and the engine stay plain
  C++17 and build on every toolchain (MinGW-w64 i686, MSVC x64, GCC x64 on Linux). File I/O belongs in
  `tools/` and `tests/`, JUCE only in `app/`.
- **The audio path is real-time safe.** Nothing in the engine's render path may allocate, lock or
  block after the boot.
- **Ensoniq material.** The only Ensoniq files in git are the six ROMs the app needs (OS v3.10
  `roms/os/ts10_310h.bin` and `ts10_310l.bin`, the four `roms/waves/*.bin`) and `roms/README.txt`,
  plus the two panel logos in `app/resources/` (the TS-10 logo and the Ensoniq badge, A122). Other OS versions, `roms/misc/`, `datasheets/` and `docs/reference/mame/` are ignored on purpose.
  Nothing is pushed to a public remote until the user approves publishing those six ROMs.
- **The panel layout** follows `datasheets/ts10-photo.webp` for arrangement and proportions and the
  TS-10 Musician's Manual page 3 ("Front Panel Controls") for the left section and the labels (the
  user's rule; design section 10).
- **Tests never touch the user's own settings or memory.**

## Building

PowerShell or Git Bash on Windows. Build folders are `build*/` (ignored).

```powershell
# Core, MinGW-w64 i686 + Ninja (the compiler is in C:\winlibs\mingw32\bin; put it on PATH first)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Core, MSVC x64
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
```

```powershell
# Core on Linux GCC x64 (in WSL: wsl -d Ubuntu -- bash -lc '...', repo under /mnt/c/...)
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux

# App (Plan 4), MSVC x64: Standalone, VST3 and the app checks. The first configure downloads JUCE
# 8.0.16-security into the build folder (git and network once); see docs/tsapp.md
cmake -S . -B build-app -G "Visual Studio 17 2022" -A x64 -DENS_BUILD_APP=ON
cmake --build build-app --config Release --target TSReborn_Standalone TSReborn_VST3 TSReborn_AppTests

# App on Linux GCC x64; -DFETCHCONTENT_SOURCE_DIR_JUCE=<juce-src> uses a JUCE checkout instead of fetching
cmake -S . -B build-linux-app -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON
cmake --build build-linux-app --target TSReborn_Standalone TSReborn_VST3 TSReborn_AppTests
```

```bash
# App on macOS (Apple clang, arm64 or x64). Unix Makefiles: Ninja is not required.
cmake -S . -B build-mac-app -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON
cmake --build build-mac-app --target TSReborn_Standalone
```

```powershell
# Release folder (Plan 6, A126): builds in build-dist and writes "dist\TS Reborn-<version>" with the six ROMs
powershell -ExecutionPolicy Bypass -File tools\dist.ps1 [-RomFolder <folder>] [-JuceSource <JUCE checkout>]

# Linux (in WSL): builds in build-dist-linux and writes "dist/TS Reborn-<version>-linux"
tools/dist.sh [--rom-folder <folder>] [--juce-source <JUCE checkout>]
```

`TSR_RELEASE_VST3=<a release folder's TS Reborn.vst3>` makes the app checks also load that package with no ROM
folder set: it must boot on its own ROMs (A125). The app and its checks never open a
window; run them headless. Never start `TS Reborn.exe` or a plugin GUI from an agent session.

## Testing

| What | Command | Notes |
|---|---|---|
| Core suite | `build\ens_tests.exe`, `build-msvc\Release\ens_tests.exe` | Must pass on **both** toolchains. |
| Real-time suite | `build\ens_rt_tests.exe`, `build-msvc\Release\ens_rt_tests.exe` | Counts allocations in `Machine::runFrame` and `Engine::render` after the boot; must be 0. Also on Linux. |
| App checks | `build-app\app\TSReborn_AppTests_artefacts\Release\TSReborn_AppTests.exe` | Headless: ROM folder, memory file, buses, parameters, state, the editor painted offscreen, the built VST3 through JUCE's host. About 55 s with the ROMs. Linux: `build-linux-app/app/TSReborn_AppTests_artefacts/Release/TSReborn_AppTests`. Sets `TSR_SETTINGS_DIR` to a temp folder. |
| 68000 suite | `ctest --test-dir build -R m68k_suite` | Needs Tom Harte's tests (`testdata/`, ignored; see `docs/m68k.md`). `-DENS_M68K_TESTS_DIR=` can point at the KS-32 repo's local copy. |
| TS boot | `build\tsemu.exe boot --ms 5300 --display-only` | Prints each display change; `--trace`, `--press`, `--analog`, `--ram <file>` (the battery RAM image; written back only after a run of at least the boot time), `--sqx70`, `--disk <image>` and `--save-disk <file>` (a disk image in the drive; the disk written out at the end), `--cd <image>` (fits the SP-4 SCSI board and puts a `.iso` or `.cue` CD image in the CD-ROM drive at ID 4, read between frames; also for `render`): see the comment at the top of `tools/tsemu/main.cpp`. |
| TS render | `build\tsemu.exe render tests/data/player_smoke.mid out.wav` | Boots, plays the MIDI file, writes Main as a WAV and prints `pcm hash <16 hex>`. |
| Speed | `build-msvc\Release\tsemu.exe bench` | Realtime factor of a 20 s chord load after the boot; must stay at x2 or more on MSVC. |

**Reference hashes** are printed, not asserted, must be identical on MSVC x64, MinGW-w64 i686 and
Linux GCC, and must not change unless a change is meant to alter emulation output. When one changes on
purpose, record the new value here and in the current verification report.

| Hash | Printed by | Value (Plan 3, unless noted) |
|---|---|---|
| Boot trace | `ts_boot_is_deterministic` (`boot trace hash`) | `ba7e34a4cf23d1e0` (OT16 free-running pot A/D, 2026-11-16; Plan 3 `3bfc7336098a6def`) |
| Render | `ts_render_is_deterministic`; `tsemu render tests/data/player_smoke.mid` | `23b6485893beb761` (OT16, 2026-11-16: the OS's analog scale is right from power-on; Plan 5 Task 4's `3374892bfdc150e6`) |
| Render | `tsemu render tests/data/full_chord.mid` | `0aa23b0ebbdeeec6` (OT16; was `524fcc64af78f175`) |
| Render | `tsemu render tests/data/filtered_patch.mid` | `e9d27e034e29b527` (OT16; was `01e3c8622cc8c8be`) |

Firmware tests read the ROMs from `ENS_ROM_DIR` (default `roms/`) and print "skip: ROMs not found"
without them. The disk tests read the user's disk images from `ENS_DISK_DIR` (a CMake cache variable, a
compile-time path; empty by default): `-DENS_DISK_DIR="<folder with AD-001.img, ED-001.GKH, the blank
Giebler files>"`. With it empty, `image_user_files` and the floppy tests that need those images print
"skip: disk images not found". The CD tests (`cd_file_reads_user_cds`, `ts_cd_*`) read the user's CD images from the
same folder. The app checks need it too to run their floppy and CD project checks: configure `build-app` and
`build-linux-app` with `-DENS_DISK_DIR=...` (it becomes `TSR_DISK_DIR` there). The images are the user's own,
only ever read, and never copied into the repo.

Tests use the tiny registry in `tests/check.h` (`TEST(name) { CHECK(...); CHECK_EQ(a, b); }`). A new
test file is one line in `ENS_TEST_SOURCES` in `tests/CMakeLists.txt`. For bugs, write the failing test
first, then fix.

Things only a person can check (sound, a MIDI keyboard, a DAW) go in the current verification report
as "user to run", not claimed as passed.

## Where things are written down

- `docs/decisions.md`: every choice the datasheets or the firmware leave open, as an entry with
  Options / Choice / Evidence / Confidence / Status (template at the top). Ids by area: `E` ESP,
  `M` 68000, `D` DUART (copied from KS-32 with `P1`, `A1`, `A2`), `OT` OTTO, `T` TS machine, `A` app
  (new entries from `A100`). Mark the code at the point of use with `// DECISION <id>: <one line>`.
  A new behaviour choice gets a new entry, not just a comment.
- `docs/ts-hardware-interface.md` (from Plan 3): what is known about the TS hardware and how it was
  found.
- `docs/design/`: one design per stage. `docs/superpowers/plans/`: the implementation plans.
- `docs/verification-planN.md`: the closing check of each plan (toolchains, hashes, manual checks,
  what is left open). Treat old plans and reports as history; correct live docs instead.
- `docs/reference/`: datasheet notes: ESP (`es5510-esp-datasheet-notes-part1/2.md`) and OTTO
  (`es5506-otto-datasheet-notes.md`, the source the `src/otto` core cites as "notes §N"). `docs/m68k.md`: the 68000
  core and its test suite (written for KS-32; the core is unchanged).

When behaviour changes, update the live guide and the decision entry in the same change.

## Style

- C++17, 4-space indent, lines up to about 120 columns, warnings clean at `/W4` and `-Wall -Wextra`.
- Comments say why, and cite their source (datasheet section, service manual page, decision id).
- Docs are plain, short sentences; no marketing.
- Commits: `feat(otto): …`, `fix(esp): …`, `test(ts): …`, `docs: …`, scoped by folder. Work on a
  plan happens on a `planN-…` branch merged into `main` with a merge commit.
