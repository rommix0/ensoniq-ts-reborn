# AGENTS.md

Guidance for coding agents working in this repository. Read [README.md](README.md) first for what the
project is; this file is how to work on it.

## The project in one paragraph

An emulator of the Ensoniq TS-10 (the TS-12 later) that runs the instrument's own OS v3.10 from the
ROMs in `roms/`. Plain C++17 chip cores (68000, ES5506 OTTO from Plan 2, ES5510 ESP, 68681 DUART)
make up a TS machine (`src/ts`, from Plan 3), a real-time engine drives it, and a JUCE 8 app wraps
that engine as a Standalone and VST3 named "TS Reborn" (`app/`, from Plan 4) for Windows x64 (MSVC)
and Linux x64 (GCC). The 68000, ESP and DUART cores were copied from KS-32 Reborn at commit
`c17c35c`; the design is `docs/design/2026-11-13-ts-emulator-design.md`.

## Rules that are not negotiable

- **Run the firmware; never study it.** The OS is run as-is in emulation. Do not disassemble,
  decompile, annotate or map the firmware, and do not propose it. Hardware behaviour comes from the
  service manual, the schematic, the chip datasheets and bus traces of the running OS. Feeding the
  running OS candidate inputs and watching how it reacts is running it, not studying it.
- **MAME is a comparison point, never a reference.** Do not copy MAME's behaviour to match it.
  `docs/reference/mame/` (git-ignored) holds the user's working copy of MAME's TS driver for
  comparison only; keep it out of the code.
- **No JUCE, OS calls or file I/O under `src/`.** The cores, the machine and the engine stay plain
  C++17 and build on every toolchain (MinGW-w64 i686, MSVC x64, GCC x64 on Linux). File I/O belongs in
  `tools/` and `tests/`, JUCE only in `app/`.
- **The audio path is real-time safe.** Nothing in the engine's render path may allocate, lock or
  block after the boot.
- **Ensoniq material.** The only Ensoniq files in git are the six ROMs the app needs (OS v3.10
  `roms/os/ts10_310h.bin` and `ts10_310l.bin`, the four `roms/waves/*.bin`) and `roms/README.txt`.
  Other OS versions, `roms/misc/`, `datasheets/` and `docs/reference/mame/` are ignored on purpose.
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

The app (Plan 4) and release-folder (Plan 5) commands are added by those plans.

## Testing

| What | Command | Notes |
|---|---|---|
| Core suite | `build\ens_tests.exe`, `build-msvc\Release\ens_tests.exe` | Must pass on **both** toolchains. |
| 68000 suite | `ctest --test-dir build -R m68k_suite` | Needs Tom Harte's tests (`testdata/`, ignored; see `docs/m68k.md`). `-DENS_M68K_TESTS_DIR=` can point at the KS-32 repo's local copy. |

**Reference hashes** start with Plan 3 (the `tsemu` boot and render). Once they exist they are
printed, not asserted, must be identical on MSVC x64, MinGW-w64 i686 and Linux GCC, and must not
change unless a change is meant to alter emulation output.

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
- `docs/reference/`: datasheet notes (ESP now; OTTO notes from Plan 2). `docs/m68k.md`: the 68000
  core and its test suite (written for KS-32; the core is unchanged).

When behaviour changes, update the live guide and the decision entry in the same change.

## Style

- C++17, 4-space indent, lines up to about 120 columns, warnings clean at `/W4` and `-Wall -Wextra`.
- Comments say why, and cite their source (datasheet section, service manual page, decision id).
- Docs are plain, short sentences; no marketing.
- Commits: `feat(otto): …`, `fix(esp): …`, `test(ts): …`, `docs: …`, scoped by folder. Work on a
  plan happens on a `planN-…` branch merged into `main` with a merge commit.
