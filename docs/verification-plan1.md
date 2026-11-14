# Plan 1 verification

Closing check of Plan 1 (`docs/superpowers/plans/2026-11-13-ts-repo-seed.md`): the repo is seeded
with the KS-32 Reborn 68000, ESP and DUART cores (KS-32 commit `c17c35c`), their tests build
warning-clean and pass on both required toolchains, and the ROM tracking matches the design.

## Test counts

| Toolchain | Configure | Result |
|---|---|---|
| MinGW-w64 g++ 14.2.0, i686, Ninja, Release | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` | `build/ens_tests`: **194 passed, 0 failed**; 0 warnings |
| MSVC 19.44.35227 (Visual Studio 2022 Build Tools), x64, Release | `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64` | `build-msvc/Release/ens_tests`: **194 passed, 0 failed**; 0 warnings |

CMake 3.31.5. Both builds were configured from empty build folders. The same 194 test names pass
on both toolchains, and each of them also passes in KS-32's own MSVC build (376 passed, 0 failed
there). The 194 are the 195 `TEST` cases of the copied files less
`reverb_tank_trace_matches_program`, which needs the OTIS trace format (not copied).

## 68000 per-instruction suite

Run on MinGW with `-DENS_M68K_TESTS_DIR` pointing at KS-32's local copy of Tom Harte's tests
(124 `.m68t` files, git-ignored in both repos):

```
total: 1000060/1000060 (smp 0, aef 31371, dev 3)
```

This is the same line as KS-32's `docs/verification-plan5.md`. `ctest -R m68k_suite`: 1/1 passed.

## Repo checks

- `git ls-files roms`: exactly `roms/README.txt`, `roms/os/ts10_310h.bin`, `roms/os/ts10_310l.bin`
  and the four `roms/waves/*.bin`. `git check-ignore` confirms the other OS versions,
  `roms/misc/` and `datasheets/` are ignored.
- Every `// DECISION <id>` cited by the code has a `## <id>` entry in `docs/decisions.md`.
- `git ls-files --eol`: no file is stored with CRLF.
- `CMakeLists.txt` and `tests/CMakeLists.txt` name no KS-32-only target (`otis`, `ks32`, `player`,
  `ens_app`, `ens_trace`).

## Not copied, and why

`src/otis` (the ES5506 OTTO replaces it), `src/player` and `tools/ks32play` (the pre-firmware test
player), `src/trace`, `tools/ens_trace`, `tools/pcm_diff` and `tests/test_trace.cpp` (the trace
format drives an OTIS), `src/ks32`, `src/app`, `app/` and `tools/ks32emu` (Plans 3-4 adapt them),
`tools/mame_ref`, `tools/dist.*` (Plan 5).

## Left open

- No ES5506 OTTO core yet (Plan 2).
- No reference hashes yet; they start with the `tsemu` boot and render in Plan 3.
- The user's approval is needed before the six ROMs are pushed to a public remote. No remote is
  set.
- Plan 1 was done on `main` (the repo did not exist before it); later plans use `planN-…` branches.
