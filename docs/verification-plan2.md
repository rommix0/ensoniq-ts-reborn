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

Checked against the running TS firmware, by its bus traces and by ear. Answered in Plan 3 Task 7 from
TS OS v3.10 on the Plan 3 machine: a scratch harness (not committed) tallied the OTTO bus events of a
boot and of a play run (one note on each of the 60 bank programs, then a 32-note chord), and read the
voices' state from copies of the OTTO. Each OT entry in `docs/decisions.md` carries the details.

- OT7: does the firmware read the filter temps of stopped voices (as the KS-32 firmware did)?
  **No.** The boot reads none; the play run reads only O1(n-1), 26 times, always of a running voice.
  The firmware writes the temps as 0 (1272 writes, none non-zero). OT7 is not observable with this
  firmware.
- OT9: the HP pole's +0.5 scale, and OT10's pole-3 routing, by listening to filtered sounds.
  **OT9's HP reading is contradicted.** The firmware writes K2 up to 0xFF00 on sounding voices with a
  high-pass pole; with the literal +0.5 the feedback gain reaches 1.5 and pole 4 sits on the 18-bit rail
  in 20 of the 60 bank programs. Not fixed in Task 7: the two stable readings (K/65536, or
  0.5 + K/131072) need a listen to choose (OT9). **OT10:** mode 11 is nearly every voice, mode 10 is in
  12 programs, and K1 differs from K2 in most samples, so the routing choice is audible; the firmware
  cannot settle it. User to run: listen to `tsemu render tests/data/filtered_patch.mid` (WOW-SWEEP, which
  uses modes 01, 10 and 11).
- OT13: saturating channel sums; OT12: exponent 0.
  **OT13 matters:** `tests/data/full_chord.mid` (16 notes at velocity 127) drives OTTO channel 0 to the
  20-bit limit in 123 samples. User to run: listen to `tsemu render tests/data/full_chord.mid` for
  clipping that does not belong. **OT12 is inaudible:** 18 of 4894 volume writes have exponent 0 and a
  non-zero mantissa, which leaves at most 16 LSB of a full-scale voice either way.
- OT16: how the firmware reads the pots (the TS selects the analog input through the DUART outputs).
  **Consistent with OT16.** The OS reads PAR about every 2 ms and changes the select within 127 slots
  after a read (24 388 of 24 398 changes), so each conversion ends on the new input and the next read
  returns it; the Analog Test page pairs each reading with its input. A sample taken when the
  conversion starts would pair it with the input before.
- OT1: the byte order and access pattern the firmware uses on the 8-bit host bus.
  **Byte accesses only, MSB first.** Reads are bytes 0-1-2-3; writes are 0-1-2-3, 2-3 (registers of
  16 bits or fewer) or 3 alone (8 bits or fewer). Every partial write covers all the implemented bits,
  so whether the write latch is shared cannot be seen with this firmware.

Render hashes (MinGW-w64 i686, `tsemu render <file> out.wav --roms roms`, default 2000 ms tail):
`tests/data/full_chord.mid` 26db4104fbbae4f4, `tests/data/filtered_patch.mid` 51b81e7658681791.
