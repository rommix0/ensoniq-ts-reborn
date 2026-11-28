# Plan 17 verification: mod-wheel crackle on WHL-DDL-BEL

2026-11-27, branch `plan17-modwheel-crackle`. Decisions T33 and E34. This is a bug fix found by ear (the user,
on U0-2 WHL-DDL-BEL), so there is no design or plan document.

## Root cause

The OS changes an effect's parameters while the ESP runs, all with one routine (c4c916):
1. Put the data in GPR $00-$03.
2. Wait for Host Access OK.
3. Wait while the ESP's PC (host register $16) reads 0x28 or more (c4ca24).
4. Write the select.
5. Read the GPR back to check it, and write it again if it differs.
6. After every transfer, write Read Select = 0xFF (ZERO), a parking read (c4c9bc).

On WHL-DDL-BEL each wheel step writes GPR 163 (the delay) and then GPR 164 (the effect's level, 0x7FFF00). The
data for 164 goes in 410 CPU clocks after the parking read. When the parking read had not finished by then, the
END copied ZERO over the data and 164 became 0 for 3 samples. Then the OS's check wrote it again. The result was
two clicks. The cause is not the host's audio blocks (a wheel set on every frame clicked the same), not the analog
path and not the OTTO.

Two things in the model let it happen:

- **T33.** The machine runs each frame's ESP sample at the frame's end, so the PC always read 0 and the OS's
  wait (step 3) did nothing. About half of the wheel's steps clicked.
- **E34.** With the PC fixed, a timer interrupt before the parking read (interrupts come back on at c4c9a8) still
  put that read anywhere in a sample. A pending read overwrote the bytes the CPU wrote after its select. The
  OS writes its data while the parking read is pending, so it relies on those bytes staying. The datasheet says
  such a write "can upset" the transfer in progress, which fits this reading. The CPU timing gives no other way
  out. The schematic clocks the MC68EC000 at 15.2 MHz (sheet 3), and DRAM and ROM have no DTACK delay (sheet
  1: R133 fitted, R132 not), so the 410 clocks are 27 µs on the board too, less than a sample.

## The fix

- T33: the host reads the PC the ESP would be at by now: the frame's elapsed share of its 103 instruction
  periods, clamped to the last END word (`Esp::setElapsed`, set by `Machine::slot`). Transfers still happen at
  the END.
- E34: a byte of $00-$08 the host writes after a read select stays, and the read fills only the others
  (`HostRequest::kept`). A new select starts afresh.

## Tests

Each test was seen failing first and passes now:

- `host_pc_follows_the_sample_time` (ESP, T33): the PC reads the elapsed periods, clamped to the END word, and 0
  while halted.
- `host_write_during_pending_read_is_kept` (ESP, E34): a byte written after a read select survives the read.
- `ts_mod_wheel_moves_the_delay_without_clicks` (firmware) plays WHL-DDL-BEL with a C and E held while the wheel
  goes up and down in 1 s, twice. It counts frames with GPR 164 at 0 and second differences of Main above 300:
  - Before T33: not measured with this sweep. The first version of the test (one note, one 2 s sweep up) had 60
    frames at 0 and 19 clicks.
  - With T33 only: 30 frames at 0, 26 clicks.
  - With both: 0 and 0.

## Other sounds

All 120 U0 sounds were tried with a C and E held and the wheel swept 0 to 1023 in 1 s. The count is the OS's GPR
read-backs that differ from what it wrote:

| Sound | Before | T33 | T33 + E34 |
|---|---|---|---|
| U0-1 LATIN-PIANO (program 6) | 7 | 2 | 0 |
| U0-6 ROCK-ORGAN1 (36) | 4 | 7 | 0 |
| U0-6 CNCRT-ORGAN (40) | 0 | 1 | 0 |
| U0-7 ST-MARKS (45) | 15 | 7 | 0 |
| U0-2 WHL-DDL-BEL (74) | 10 | 2 | 0 |
| U0-5 RAIN-FOREST (91) | 0 | 1 | 0 |
| All 120 | 36 | 20 | 0 |

On WHL-DDL-BEL there are also none with MIDI controller 1 every 64 frames, or with the wheel stepped every 54 ms
as a host block steps it. Under T33 alone those runs had 2 and 7 mismatches (11 and 24 clicks).

## Toolchains

| | Core (`ens_tests`) | Real-time (`ens_rt_tests`) | App checks |
|---|---|---|---|
| MinGW-w64 i686 | 445 passed | 8 passed | - |
| MSVC x64 | 445 passed | 8 passed | all passed |
| Linux GCC x64 (WSL) | 445 passed | 8 passed | all passed |

The fix changes emulation output on purpose: the OS's ESP writes land in other samples, and the boot and the
test renders hit the same race. These are the new reference hashes, identical on the three toolchains:

| Hash | Before | After |
|---|---|---|
| Boot trace | `ba7e34a4cf23d1e0` | `f1e464f1a074898f` |
| Render `player_smoke.mid` | `23b6485893beb761` | `62ba6a4ce86d4cb4` |
| Render `full_chord.mid` | `0aa23b0ebbdeeec6` | `7e636e9b602ee5db` |
| Render `filtered_patch.mid` | `e9d27e034e29b527` | `2f07609f83dbede1` |

`tsemu bench` (MSVC): realtime x3.64.

## Open

- E34 is read from the OS's behaviour and one datasheet sentence. The chip's latch logic is not documented.
- Where in the frame the real ESP's END falls is not known; it is taken at the frame's end.

## User to run

- WHL-DDL-BEL (U0-2, the third sound): hold a note or a chord and move the mod wheel up and down, slowly and
  fast, with the on-screen wheel and with a MIDI keyboard's wheel. The delay should move with no crackle.
- The same fast moves on ST-MARKS (U0-7), LATIN-PIANO (U0-1) and ROCK-ORGAN1 (U0-6).
- A few sounds without wheel effects (BABY-GRAND, a pad) still sound as before.
