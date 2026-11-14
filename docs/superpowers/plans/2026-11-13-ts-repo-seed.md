# TS Repo Seed (Plan 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `C:\Users\abart\Desktop\Ensoniq TS` into the git repo `ensoniq-ts-reborn`, seeded with copies of the KS-32's finished chip cores (68000, ES5510 ESP, 68681 DUART) and their tests, building warning-clean and passing on MSVC x64 and MinGW-w64 i686.

**Architecture:** A straight copy of the KS-32's `src/common`, `src/m68k`, `src/esp`, `src/duart`, their rule tests and two tools, with a trimmed top-level CMake. Nothing TS-specific is written yet: OTTO is Plan 2, the machine Plan 3, the app Plan 4. The KS-32 repo is read-only for this plan.

**Tech Stack:** C++17, CMake 3.20+, Ninja + MinGW-w64 i686 (`C:\winlibs\mingw32\bin`), Visual Studio 17 2022 Build Tools (MSVC x64), git.

**Spec:** `docs/design/2026-11-13-ts-emulator-design.md` (section 11, item 1).

**Source repo:** `KS = C:\Users\abart\Desktop\Ensoniq KS-32` (commit `c17c35c`). Copy from its working tree; never edit it.

## Global Constraints

- C++17; CMake minimum 3.20; warnings clean at `/W4` (MSVC) and `-Wall -Wextra` (GCC).
- No JUCE, OS calls or file I/O under `src/`. File I/O only in `tools/` and `tests/`.
- Repo name `ensoniq-ts-reborn`; future names `ens::ts`, `src/ts`, `tsemu`; app/plugin "TS Reborn". Decision-id areas as KS-32 plus `OT` (OTTO) and `T` (TS machine).
- License AGPLv3 (copy `KS/LICENSE` unchanged).
- Committed ROMs: exactly `roms/os/ts10_310h.bin`, `roms/os/ts10_310l.bin`, `roms/waves/901301_u43.bin`, `roms/waves/901302_u46.bin`, `roms/waves/901401_u41.bin`, `roms/waves/901402_u45.bin`, plus `roms/README.txt`. Other OS versions, `roms/misc/`, `datasheets/` and `docs/reference/mame/` stay local (git-ignored). No remote is added and nothing is pushed in this plan.
- Copied code is copied byte for byte except where a task says otherwise. Its `// DECISION <id>` comments keep their KS-32 ids.

## Review Focus

1. **A `// DECISION` id cited by copied code with no entry in the new `docs/decisions.md`.** Task 3 Step 3 checks every cited id has a `## <id>:` heading.
2. **The wrong ROMs tracked:** a non-3.10 OS image or the MAME font committed, or one of the six needed files ignored. Task 1 Step 4 checks `git ls-files roms` is exactly the seven paths.
3. **Leftover KS-32 build wiring** (`ens_otis`, `ens_ks32`, `ENS_ROM_DIR` defaulting to `ks-32`) that only fails on a clean configure. Task 2 Step 6 configures from an empty build folder and greps the CMake files.
4. **MSVC and MinGW disagreeing**, e.g. a test passing on one only. Task 4 runs the same suite on both and compares the pass counts.
5. **CRLF files committed despite `eol=lf`.** Task 4 Step 4 checks `git ls-files --eol` shows no `i/crlf`.

---

### Task 1: Repo skeleton and ROM tracking

**Files:**
- Create: `.gitignore`, `.gitattributes`, `LICENSE` (copy of `KS/LICENSE`)
- Track: `roms/README.txt`, the six ROMs above, `docs/design/2026-11-13-ts-emulator-design.md`, this plan

**Interfaces:**
- Produces: a git repo on branch `main` with one commit; the ignore rules every later task relies on.

- [ ] **Step 1: `git init -b main`** in `C:\Users\abart\Desktop\Ensoniq TS`. Expected: `Initialized empty Git repository`.

- [ ] **Step 2: Write `.gitattributes`** as `KS/.gitattributes` (`* text=auto eol=lf`) plus `*.bin binary`.

- [ ] **Step 3: Write `.gitignore`:**

```
build*/
dist/
testdata/
.worktrees/
.superpowers/
__pycache__/
*.wav
*.pcm
datasheets/
docs/reference/mame/
roms/misc/
roms/os/*
!roms/os/ts10_310h.bin
!roms/os/ts10_310l.bin
```

- [ ] **Step 4: Check the ROM tracking.** Run `git add -A --dry-run roms` . Expected: exactly `roms/README.txt`, `roms/os/ts10_310h.bin`, `roms/os/ts10_310l.bin` and the four `roms/waves/*.bin`. And `git check-ignore roms/os/ts10_202h.bin roms/misc/esqvfd_font_vfx.bin datasheets/ts10-photo.webp` prints all three paths.

- [ ] **Step 5: Copy `KS/LICENSE`, then commit.**

```bash
git add .gitignore .gitattributes LICENSE roms docs/design docs/superpowers
git commit -m "chore: repo skeleton, TS-10 OS v3.10 and wave ROMs, design and plan 1"
```

### Task 2: Copy the chip cores, their tests and tools

**Files:**
- Copy unchanged from `KS`: `src/common/`, `src/m68k/`, `src/esp/`, `src/duart/`; `tests/main.cpp`, `tests/check.h`, `tests/m68k_test_bus.h`, `tests/test_bits.cpp`, `tests/test_esp_agen.cpp`, `tests/test_esp_alu.cpp`, `tests/test_esp_core.cpp`, `tests/test_esp_host.cpp`, `tests/test_esp_isa.cpp`, `tests/test_esp_mac.cpp`, `tests/test_m68k_alu.cpp`, `tests/test_m68k_cpu.cpp`, `tests/test_m68k_dis.cpp`, `tests/test_m68k_harte_format.cpp`, `tests/test_m68k_irq.cpp`, `tests/test_duart.cpp`, `tests/data/m68k_harte_sample.json`, `tests/data/m68k_harte_sample.m68t`; `tools/esp_dis/`, `tools/m68k_harte/`
- Copy then modify: `tests/test_esp_programs.cpp` (drop the trace test)
- Create: `CMakeLists.txt`, `tests/CMakeLists.txt`

Not copied, with the reason: `src/otis` (OTTO replaces it, design section 8), `src/player` and `tools/ks32play` (pre-firmware test player), `src/trace`, `tools/ens_trace`, `tools/pcm_diff`, `tests/test_trace.cpp` (the trace format drives an OTIS; an OTTO trace, if Plan 2 wants one, is its own), `src/ks32`, `src/app`, `app/`, `tools/ks32emu` (Plans 3-4 adapt them), `tools/mame_ref`, `tools/dist.*` (Plan 5).

**Interfaces:**
- Produces CMake targets: `ens_common` (INTERFACE, include dir `src`), `ens_esp`, `ens_m68k`, `ens_duart`, `ens_m68k_harte`, executables `ens_tests`, `esp_dis`, `m68k_harte`; CTest tests `ens_tests` and `m68k_suite`; cache var `ENS_M68K_TESTS_DIR` (default `${CMAKE_SOURCE_DIR}/testdata/680x0/68000/m68t`, the suite skips when absent) and `ENS_ROM_DIR` (default `${CMAKE_SOURCE_DIR}/roms`, unused until Plan 3 but defined now so tests can take it as `ENS_ROM_DIR`). Option `ENS_BUILD_APP` is not defined until Plan 4.

- [ ] **Step 1: Copy the files listed above** (`cp -r` from `KS`; keep paths).

- [ ] **Step 2: Edit `tests/test_esp_programs.cpp`:** delete `TEST(reverb_tank_trace_matches_program)` and its preceding comment, the `#include "trace/trace.h"` line, and any `#include` (`<fstream>`, `<sstream>`) and helper left unused by that removal. The other two tests stay byte-identical.

- [ ] **Step 3: Write `CMakeLists.txt`** from `KS/CMakeLists.txt`: `project(ens_ts CXX)`; keep the C++17, warning, output-dir and `enable_testing()` lines; keep the `ens_common`, `ens_esp`, `ens_m68k`, `ens_duart`, `ens_m68k_harte` libraries and the `esp_dis` and `m68k_harte` executables and `add_test(NAME m68k_suite ...)` exactly as there; `ENS_ROM_DIR` default `${CMAKE_SOURCE_DIR}/roms` with description "TS ROM directory (roms/, see roms/README.txt)"; drop every other target and option.

- [ ] **Step 4: Write `tests/CMakeLists.txt`:** `ENS_TEST_SOURCES` = `main.cpp`, `test_bits.cpp`, the seven `test_esp_*.cpp`, the five `test_m68k_*.cpp`, `test_duart.cpp`; `ens_tests` links `ens_esp ens_m68k_harte ens_m68k ens_duart ens_common`; definitions `ENS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"`, `ENS_ROM_DIR="${ENS_ROM_DIR}"`, `_CRT_SECURE_NO_WARNINGS`; `add_test(NAME ens_tests COMMAND ens_tests)`. No `ens_rt_tests` yet (it tests the engine, Plan 4).

- [ ] **Step 5: Build with MinGW i686 from an empty folder.**

```bash
export PATH="/c/winlibs/mingw32/bin:$PATH"
rm -rf build && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build 2>&1 | tee build/build.log
grep -ci "warning" build/build.log
```

Expected: build succeeds; the warning count is `0`.

- [ ] **Step 6: Run the suite and check for leftovers.**

Run: `build/ens_tests.exe | tail -1`
Expected: `194 passed, 0 failed` (195 tests in the copied files, less the one removed in Step 2).
Run: `grep -nE "otis|ks32|ks-32|player|ens_app|ens_trace" CMakeLists.txt tests/CMakeLists.txt`
Expected: no output.

- [ ] **Step 7: Commit.**

```bash
git add CMakeLists.txt src tests tools
git commit -m "feat: seed the 68000, ESP and DUART cores and their tests from KS-32 c17c35c"
```

### Task 3: Project docs

**Files:**
- Create: `docs/decisions.md`, `AGENTS.md`, `README.md`
- Copy unchanged from `KS`: `docs/m68k.md`, `docs/reference/es5510-esp-datasheet-notes-part1.md`, `docs/reference/es5510-esp-datasheet-notes-part2.md`

**Interfaces:**
- Consumes: the `// DECISION` comments in the code from Task 2.
- Produces: `docs/decisions.md` with the template at the top and the areas `OT` (OTTO) and `T` (TS machine) ready for Plans 2-3.

- [ ] **Step 1: Build `docs/decisions.md`.** Its top is `KS/docs/decisions.md` lines 1-16 (the title and the entry template) unchanged, then this note, then the copied entries:

```markdown
> Entries E*, M*, D*, P1, A1 and A2 below are copied verbatim from KS-32 Reborn
> (`docs/decisions.md` at commit c17c35c) because the copied cores cite them. New entries for this
> project use the areas OT (OTTO) and T (TS machine), and A for the app from Plan 4 on, numbered
> from A100 so they cannot collide with the copied A1/A2.
```

The copied entries are, in KS-32 file order, every `## <id>...` section whose id is in the set cited by the code. Extract them with a throwaway script (scratch folder, not committed): split `KS/docs/decisions.md` at lines starting `## `, keep a section when its heading matches `^## (E\d+|M\d+|D\d+|P1|A1|A2)\b`, and keep the area headings `## ESP`, `## 68000 core`, `## DUART` in front of their first kept entry. Note that ids `E18` and `E19` each have several parts ("E18 (FIFO part)", etc.); keep all parts.

- [ ] **Step 2: Add the empty areas** at the end: `## OTTO` and `## TS machine`, each with the line `(None yet.)`.

- [ ] **Step 3: Check every cited id has an entry.**

```bash
for id in $(grep -rhoE "DECISION [A-Z]+[0-9]+" src tests tools | awk '{print $2}' | sort -u); do
  grep -qE "^## $id( |:)" docs/decisions.md || echo "missing $id"; done
```

Expected: no output.

- [ ] **Step 4: Write `AGENTS.md`** by adapting `KS/AGENTS.md`, keeping its sections and rules word for word except:
  - "The project in one paragraph": the Ensoniq TS-10 (TS-12 later), OS v3.10 from `roms/`; cores 68000, ES5506 OTTO (Plan 2), ES5510 ESP, 68681 DUART; machine `src/ts`; JUCE app "TS Reborn".
  - "Ensoniq material": the six ROMs plus `roms/README.txt` are the only Ensoniq files in git; `datasheets/` and `docs/reference/mame/` are ignored on purpose.
  - "Building": the core commands (MinGW Ninja, MSVC `Visual Studio 17 2022` x64) only; the app and dist commands are added by Plans 4-5.
  - "Testing": rows for the core suite (MSVC and MinGW) and the 68000 suite; the reference-hash paragraph says the hashes start with Plan 3 (`tsemu` boot and render) and lists none yet.
  - "Where things are written down": decision areas `OT`, `T` and `A` (from A100), the hardware doc `docs/ts-hardware-interface.md` (from Plan 3), the design `docs/design/2026-11-13-ts-emulator-design.md`.
  - A line under "Rules": the panel layout follows `datasheets/ts10-photo.webp` and the TS-10 Musician's Manual page 3 (the user's rule; design section 10).

- [ ] **Step 5: Write `README.md`:** title "Ensoniq TS Reborn"; one paragraph on what it will be (design section 1); a "Status" line ("Plan 1: chip cores seeded from KS-32 Reborn; no TS machine yet"); the ROM table from `roms/README.txt` (v3.10 and the four wave ROMs, with sizes); the two core build commands; the License section of `KS/README.md` with "KS-32" replaced by "TS-10 and TS-12" in the trademark line and the MAME sentence removed.

- [ ] **Step 6: Commit.**

```bash
git add AGENTS.md README.md docs/decisions.md docs/m68k.md docs/reference
git commit -m "docs: AGENTS, README, decisions carried over from KS-32"
```

### Task 4: Both toolchains and the verification report

**Files:**
- Create: `docs/verification-plan1.md`

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: MSVC x64 from an empty folder.**

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
build-msvc\Release\ens_tests.exe | Select-Object -Last 1
```

Expected: no warnings in the build output; `194 passed, 0 failed`.

- [ ] **Step 2: The 68000 suite, using KS-32's local test data** (it is git-ignored there and here; not copied).

```bash
cmake -S . -B build -DENS_M68K_TESTS_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/testdata/680x0/68000/m68t"
ctest --test-dir build -R m68k_suite --output-on-failure
```

Expected: `100% tests passed`. Record the summary line `m68k_harte` prints.

- [ ] **Step 3: Compare with KS-32.** Run `KS/build-msvc/Release/ens_tests.exe` (already built there; do not rebuild KS-32) and check every test name that passes here also passes there. Expected: same result for all 194 names.

- [ ] **Step 4: Line endings.** Run `git ls-files --eol | grep "i/crlf"`. Expected: no output.

- [ ] **Step 5: Write `docs/verification-plan1.md`** in the shape of `KS/docs/verification-plan1.md`: toolchains and versions, the pass lines from Steps 1, 2 and Task 2 Step 6, the 68000 suite summary, the ROM tracking check from Task 1 Step 4, what was not copied and why (Task 2 list), and what is left open: no ES5506 yet, no reference hashes yet, the user's approval of pushing the six ROMs to a public remote.

- [ ] **Step 6: Commit.**

```bash
git add docs/verification-plan1.md
git commit -m "docs: plan 1 verification report"
```
