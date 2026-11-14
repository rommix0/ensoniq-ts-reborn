# Plan 2 verification

Closing check of Plan 2 (`docs/superpowers/plans/2026-11-13-otto-core.md`): the ES5506 OTTO core
in `src/otto`, written from `docs/reference/es5506-otto-datasheet-notes.md`, with rule tests on both
required toolchains. No MAME comparison was made; MAME was not consulted.

## Test counts

| Toolchain | Result |
|---|---|
| MinGW-w64 g++ 14.2.0, i686, Ninja, Release | `build/ens_tests`: **238 passed, 0 failed**; 0 warnings |
| MSVC 19.44.35227 (Visual Studio 2022 Build Tools), x64, Release | `build-msvc/Release/ens_tests`: **238 passed, 0 failed**; 0 warnings |

The same 238 test names pass on both: the 194 from Plan 1 and 44 new `otto_` tests in
`tests/test_otto_regs.cpp` (7), `test_otto_addr.cpp` (15), `test_otto_signal.cpp` (7),
`test_otto_mix.cpp` (12) and `test_otto_pot.cpp` (3). Every pinned value was computed before the
code existed; the compressed-sample decoder re-encodes to the same code for all 65 536 words.

## Decisions

Every `// DECISION OT<n>` in `src/otto` has an entry in `docs/decisions.md`; OT20 has no code by
design. Confidence as recorded:

| Id | Topic | Confidence |
|---|---|---|
| OT1 | Host byte latching (shared latches, byte 0 reads, byte 3 commits) | Medium |
| OT2 | Pages and unused bits | Low-medium |
| OT3 | Reset state | Low |
| OT4 | Address generation and loop modes | Medium |
| OT5 | Fetch addresses | Medium |
| OT6 | Compressed sample decoder | Medium |
| OT7 | Stopped voices still fetch and filter, add nothing | Low |
| OT8 | Interpolation | Medium |
| OT9 | Filter arithmetic | LP medium, HP low |
| OT10 | Filter routing (the p9 table over the p34 drawing) | Medium |
| OT11 | Filter output to the pan stage | Medium |
| OT12 | Volume (exponent 0 is not a mute) | Medium |
| OT13 | Channels saturate at 20 bits, no 23-bit wrap | Low / medium |
| OT14 | Interrupts and IRQV | Medium |
| OT15 | Envelopes | Medium-low |
| OT16 | Pot A/D, 512-slot conversion | Medium |
| OT17 | Writes take effect at once | Medium |
| OT18 | Serial port as whole frames, one OTTO | High for the TS |
| OT19 | Fewer than five voices allowed | Medium |
| OT20 | Host transfers to sound memory left to the board | High |

## Left for Plan 3 (the TS machine)

Checked against the running TS firmware, by its bus traces and by ear:

- OT7: does the firmware read the filter temps of stopped voices (as the KS-32 firmware did)?
- OT9: the HP pole's +0.5 scale, and OT10's pole-3 routing, by listening to filtered sounds.
- OT13: saturating channel sums; OT12: exponent 0.
- OT16: how the firmware reads the pots (the TS selects the analog input through the DUART outputs).
- OT1: the byte order and access pattern the firmware uses on the 8-bit host bus.
