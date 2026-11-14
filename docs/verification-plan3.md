# Plan 3 verification

Closing check of Plan 3 (`docs/superpowers/plans/2026-11-13-ts-machine.md`): the TS-10 machine in
`src/ts` running the unmodified OS v3.10 on the copied 68000, ESP and DUART cores and the Plan 2 OTTO
core, and the `tsemu` tool. It covers the test suite on both toolchains, the reference hashes, the
speed, the decisions with their confidences, the hardware facts found and how, the answers to Plan 2's
open questions, and what is left for Plan 4 and for a person to check.

The facts and how each was found are in `docs/ts-hardware-interface.md`; the decisions in
`docs/decisions.md` (`## OTTO`, `## TS machine`). The firmware was run, never studied: no disassembly,
no annotation, no ROM searches. MAME was not consulted.

## Test counts

Both builds were made in empty build folders from the commit that holds this report, with the ROMs in
`roms/` (the default `ENS_ROM_DIR`), so every firmware test runs and none skips.

| Toolchain | Configure | Result |
|---|---|---|
| MinGW-w64 g++ 14.2.0, i686, Ninja, Release | `cmake -S . -B build-v3 -G Ninja -DCMAKE_BUILD_TYPE=Release` | `build-v3\ens_tests.exe`: **287 passed, 0 failed**, 35 s; 0 warnings (`-Wall -Wextra`) |
| MSVC 19.44.35227 (Visual Studio 2022 Build Tools), x64, Release | `cmake -S . -B build-msvc-v3 -G "Visual Studio 17 2022" -A x64` | `build-msvc-v3\Release\ens_tests.exe`: **287 passed, 0 failed**, 33 s; 0 warnings (`/W4`) |
| Linux GCC x64 | not run | No Linux toolchain on this machine. User to run (see the last section). |

The same 287 names pass on both (the order of registration differs between the two linkers). Plan 2
ended at 238; Plan 3 added 49 `ts_` tests in `tests/test_ts_roms.cpp`, `test_ts_board.cpp`,
`test_ts_machine.cpp`, `test_ts_panel.cpp` and `test_ts_firmware.cpp`. The 68000 per-instruction suite
(`m68k_harte` on the KS-32 repo's copy of Tom Harte's tests) passes 1 000 060 of 1 000 060 on both
builds; the 68000 core is unchanged since Plan 1.

## Reference hashes

Printed, not asserted. Identical on both builds:

| Hash | Printed by | MinGW i686 | MSVC x64 |
|---|---|---|---|
| Boot trace (FNV-1a over every bus event of the 5300 ms boot: cycle, address, value, size, direction, device) | `ts_boot_is_deterministic` | `ebf5f9eb5c643976` | `ebf5f9eb5c643976` |
| Render (boot, `tests/data/player_smoke.mid`, 2 s tail; FNV-1a of Main) | `ts_render_is_deterministic` | `4a751109deae8b89` | `4a751109deae8b89` |
| `tsemu render tests/data/player_smoke.mid out.wav --roms roms` | `pcm hash` | `4a751109deae8b89` | `4a751109deae8b89` |
| `tsemu render tests/data/full_chord.mid` | `pcm hash` | `524fcc64af78f175` | `524fcc64af78f175` |
| `tsemu render tests/data/filtered_patch.mid` | `pcm hash` | `01e3c8622cc8c8be` | `01e3c8622cc8c8be` |
| The smoke WAV file, SHA-256 | `sha256sum` | `5d9dfb63…3c3509636` | `5d9dfb63…3c3509636` |

The full SHA-256 is `5d9dfb63f8b8a6182ad3780b4fb71eb1512cbbb83230d20398b55da3c3509636`. The battery RAM
images written by the same `tsemu boot --ram` runs on the two builds are byte-identical too.

Task 8's memory map change (SEQEXP at 0x038000, T13) changed none of these: RAM accesses are not bus
events, and the OS takes the same path in the boot (its SQX-70 probe passed before too).

## Speed (`tsemu bench`)

`tsemu bench --roms roms` boots (timed on its own line), then plays an 8-note chord (MIDI notes 36, 43,
48, 52, 55, 60, 64, 67, velocity 100) from MIDI in at the start of every second, off 750 ms later, for
20 emulated seconds; the audio is discarded. Three runs per build, one after the other:

| Build | Chord loop, 20 s emulated | Boot, 5.3 s emulated |
|---|---|---|
| MSVC x64 Release | host 5.15 to 5.29 s, **realtime x3.78 to x3.88** | host 0.89 to 0.91 s, x5.80 to x5.95 |
| MinGW i686 Release | host 5.38 s, **realtime x3.72** | host 0.94 s, x5.63 to x5.65 |

Every run printed peak 21789. The plan asked for x2 or more on MSVC: it is about x3.8.

## Decisions

Confidence is the entry's own after the last task that touched it.

### TS machine (`## TS machine`)

| Id | Decision | Confidence |
|---|---|---|
| T1 | CPU = OTTO = 15 238 090 Hz (the schematic's digit is unreadable; the KS-32's crystal); DUART X1 4 MHz; serial clocks 500 kHz (MIDI) and 1 MHz (panel link); ESP 9.216 MHz; slot scheduler with carried overshoot | Low-medium for the CPU clock (the OS's clock check passes from 3.7 to 4.7 MHz X1); medium for the serial clocks; the scheduling is exact |
| T2 | OS image: h even bytes, the first 256 KB of l odd bytes | Medium-high |
| T3 | Wave memory layout of banks 0 and 1; U45/U46 first byte high | Medium (layout), high (byte order) |
| T4 | Sample RAM: 2 MB in bank 2, CPU window 0x400000-0xBFFFFF | Medium for the window; low for the size and empty-socket reads |
| T5 | OTTO level 1, DUART level 3, all autovectored | High for the DUART; medium-high for the OTTO |
| T6 | No wait states | Low |
| T7 | DUART wiring: A = MIDI, B = panel link, OP0-2 mux, OP3 SEQLED/, OP4 KRES, OP5 DRES, OP6 ESPHALT (active high), OP7 SACK/ | High for the signals; medium for the OP6 polarity |
| T8 | Analog inputs and their resting values | High for the path; medium for the resting values |
| T9 | IEXP: D0-D3 active low, D4-D7 high | High for D0-D3 |
| T10 | Empty drive: no index pulse; the FDC handshake | High for the empty-drive behaviour; low for ST0/ST3 and IP1/IP2 |
| T11 | Audio routing: OTTO ch0-2 to the ESP, ch3 to Aux, `sat16(v20 >> 4)`, 103 ESP instructions a frame | Medium-low for the bit alignment (by ear in Plan 4) |
| T12 | SCSI and unmapped space read 0 | Medium |
| T13 | Address map; OSRAM 256 KB at 0-0x37FFF and its top 32 KB at 0xFF8000; SEQEXP (the SQX-70) fitted at 0x038000-0x077FFF; the battery image OSRAM then SEQEXP | High for the device windows and the low RAM window; medium for the top window; medium-high for SEQEXP |
| T14 | RESET resets the OTTO only | Medium |
| T15 | LED states: bit 7 lit, bit 6 flashing; LED numbering | High for lit and the numbering; medium for flashing |
| T16 | Button codes and forms | High; medium for Compare and the foot switch naming |
| T17 | Keys and Poly-Key pressure | High for the forms; low for what the strike value is physically |
| T18 | Display command set | High for text, positions and the main commands; medium for the attributes; low for `7F` and `E7` |
| T19 | Keyboard start-up (`FF` to `FB FD`), KRES/DRES resets, SACK/ gating | High for `FF` and the resets; **low for SACK/'s role** |
| T20 | MIDI in on DUART A's wire queue, MIDI out popped from TxDA | High |

### OTTO entries revisited in Plan 3

- **OT9** (filter arithmetic): the HP feedback is 0.5 + K/131072 (controller ruling in Task 7). The
  literal +0.5 put pole 4 on the 18-bit rail in 20 of the 60 bank programs; the ruling keeps the
  datasheet's 0.5 floor. LP medium, **HP low** until heard.
- OT1, OT7, OT10, OT12, OT13 and OT16 gained the firmware's evidence (below); their choices did not
  change.

## Hardware facts found, and how

Each is in `docs/ts-hardware-interface.md` with how it was found, the alternatives and the confidence.
The method throughout: `tsemu boot` with `--trace`, `--press` and `--analog`, and scratch harnesses
(not committed) that printed time lines of device accesses, exceptions, DUART bytes and display text,
one change at a time. "Gets further" meant more kinds of device access, fewer unmapped accesses or
fault vectors, and no "SYSTEM ERROR n" text.

- **Memory map** (T13, T4): the OS's first accesses land in the U7 decoder's order, with 0 unmapped
  accesses in the boot. The sample-RAM window from the OS's socket probe at 107.5 ms.
- **SEQEXP** (T13, Task 8): the boot's one probe at 0x038000; the Sequencer Control page's FREE
  (97 311 with RAM there, 31 775 without, the manual's 97 000 and 30 000 notes); and where a kept
  sequence is written (from 0x077FFF down with the probe answered, 0x037FFF without).
- **Battery RAM** (T13, Task 8): Master Tune survives a power cycle from the image; garbage RAM is a
  cold start (identical display sequence to the cold boot, no fault).
- **Interrupts** (T5): every placement booted; the DUART on 1 or 2 gives SYSTEM ERROR 17 or 139.
- **Clocks** (T1): the OS's boot clock check; CSRA = CSRB = 0xEE needs the external serial clocks.
- **ESPHALT** (T7): the OS halts the ESP, downloads to it, releases it; the reverse polarity leaves it
  halted.
- **OTTO voice count**: the OS writes ACTV = 0x1F (32 voices) twice in the boot.
- **Analog inputs** (T8): the Analog Test page (hold Presets, press Compare) with one input stepped at
  a time.
- **IEXP** (T9): the OS sends the patch selects as MIDI controller 70 and Foot Switch 2 as sustain.
- **Floppy** (T10): with no index pulse the OS reports "DISK DRIVE NOT READY", the manual's no-disk
  answer.
- **ILLEGAL at boot**: two ILLEGAL instructions at about 1017 ms, stepped over by the OS's own handler.
- **Keyboard link** (T19): of all 256 bytes, only `FF` ends the OS's "CALIBRATING KEYBOARD" wait.
- **Display and LEDs, buttons, keys** (T15-T18): every button code swept from the play screen and
  named by the page, LED or MIDI message it gives, checked against the Musician's Manual and the
  service manual's self-test chart.
- **MIDI** (T20): note on, off, a 198-byte burst with no overrun, program change.

## Plan 2's questions, answered

From `docs/verification-plan2.md` ("Left for Plan 3"), answered in Task 7 from the running OS:

| Item | Answer |
|---|---|
| OT7: does the firmware read stopped voices' filter temps? | No; OT7 is not observable with this firmware |
| OT9: the HP pole's +0.5 | Wrong as read; fixed to 0.5 + K/131072 (no program on the rail). Listening: user to run |
| OT10: pole-3 routing | Audible (mode 10 in 12 programs, K1 differs from K2); the firmware cannot settle it. Listening: user to run |
| OT13: saturating channel sums | Matters: `full_chord.mid` drives channel 0 to the 20-bit limit in 123 samples. Listening: user to run |
| OT12: exponent 0 | Inaudible (18 of 4894 volume writes, at most 16 LSB) |
| OT16: how the pots are read | Consistent: each conversion ends on the newly selected input |
| OT1: host bus access pattern | Byte accesses only, MSB first; a shared write latch cannot be seen |

## Controller rulings

From the plan's ledger, with the cost if wrong:

| Task | Ruling | Cost if wrong |
|---|---|---|
| Pre-flight | `tests/ts_render.h` is shared by the tests and `tsemu` (tests/ on its include path) | A file move later |
| 3 | `cycles()` is the raw CPU total; the cadence test allows 51 240 to 51 400 cycles over 100 frames for NOP, DIVU and STOP | A test window to retune |
| 4 | Vector 4 is allowed only at an ILLEGAL opcode (the OS raises it on purpose) | A real illegal-instruction fault hidden only if it is exactly 0x4AFC |
| 4 | The chips test checks both receivers enabled and both transmitters having sent | None material |
| 4 | Battery image OSRAM only until SEQEXP is found; Task 8 to look | Superseded in Task 8: SEQEXP found and in the image |
| 5 | `setAnalog` added early with Task 6's interface; `PanelState` carries `underline[2]` | Plan 4 snapshot size |
| 7 | OT9 HP feedback = 0.5 + K/131072, i.e. `((k12 + 4096) * y1) >> 13` | HP timbre off; every render hash changes again |
| 8 | No merge by the implementer; the controller merges after the final review | None |

## Left for Plan 4 (the engine and the app)

- The real-time engine around `Machine::runFrame`, with the allocation counter test (no allocation in
  `runFrame` and below is a rule now, not yet a test).
- **Data Entry slider never at raw 0**: the OS then rewrites the selected value at its minimum and the
  Analog Test page shows SYSTEM ERROR 144. The app's slider must map to 1-1023.
- **MIDI in bursts over 1024 bytes** (a large sysex) are dropped beyond DUART A's wire queue and
  counted in `duart().rxDropped`, but nothing surfaces them. The engine must pace MIDI in or report it.
- The battery RAM: the app keeps the image (`ramImage`, `loadRamImage`) in its state and on disk.
  Loading clears the sample DRAM, as a power cycle does.
- Disk: the drive is empty; FDC register writes (A0 = 0, the 72069's own registers) are not modelled.
  SCSI is absent.
- A stock TS (no SQX-70: open bus at 0x038000) is not modelled; the machine always has the SQX-70.
- Display attributes `E7` and `7F` (effect unknown); SACK/'s role (low confidence); wait states (T6)
  if a timing-sensitive path shows up; the CPU clock (T1) is loosely pinned.
- Levels: the bit alignment of the OTTO-to-ESP and Aux paths (T11) is a guess until heard.

## User to run

Nothing below has been checked by a person. Render each file with `tsemu render <file> out.wav --roms
roms` and listen:

- **Overall levels** (T11, OT13): `tests/data/player_smoke.mid` and `tests/data/full_chord.mid`
  (16 notes at velocity 127): no clipping that does not belong, a sensible level against a real TS.
- **OT9, the high-pass filter**: programs with HP poles, e.g. program 8 (HONKY-TONK) and program 50
  (WOW-SWEEP).
- **OT10, filter routing**: `tests/data/filtered_patch.mid` (WOW-SWEEP, filter modes 01, 10 and 11).
- **Linux GCC x64**: build and run `ens_tests` and the three renders; the hashes above must match.
- With a real TS-10 at hand: its Sequencer Control FREE reading (31 775 stock, 97 311 with an SQX-70
  are this emulator's) would confirm T13's SEQEXP reading.
