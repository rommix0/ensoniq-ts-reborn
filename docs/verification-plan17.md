# Plan 17 verification: mod-wheel crackle on WHL-DDL-BEL

2026-11-27, branch `plan17-modwheel-crackle`. Decision T33. A bug fix found by ear (the user, on U0-2
WHL-DDL-BEL); no design or plan document.

## Root cause

The OS changes an effect's parameters while the ESP runs with one routine (c4c916): it puts the data in GPR
$00-$02, waits while the ESP's PC (host register $16) reads 0x28 or more, writes the select, reads the GPR back to
check, and after every transfer writes Read Select = 0xFF (ZERO) as a parking read. The machine runs each frame's
ESP sample at the frame's end, so the PC always read 0 and the OS's wait did nothing. On WHL-DDL-BEL each wheel step
writes GPR 163 (the delay) and then GPR 164 (the effect's level, 0x7FFF00) 410 CPU clocks after the parking read.
When both fell in one sample, the parking read ended after 164's data went in, the END copied ZERO over it and 164
became 0 for 3 samples until the OS's check wrote it again: two clicks, on about half the wheel's steps. Not the
host block quantisation (a wheel set every frame clicked the same), not the analog path, not the OTTO.

## The fix

The host reads the PC the ESP would be at by now: the frame's elapsed share of its 103 instruction periods,
clamped to the last END word (`Esp::setElapsed`, set by `Machine::slot`). Transfers still happen at the END.

## Tests

Seen failing first, passing after:

- `host_pc_follows_the_sample_time` (ESP): the PC reads the elapsed periods, clamped to the END word, 0 halted.
- `ts_mod_wheel_moves_the_delay_without_clicks` (firmware): WHL-DDL-BEL, middle C, a 2 s wheel sweep from 0 to
  1023 set every frame. Before: GPR 164 at 0 for 60 frames, 19 second differences of Main above 300. After: 0
  and 0.

## Toolchains

| | Core (`ens_tests`) | Real-time (`ens_rt_tests`) | App checks |
|---|---|---|---|
| MinGW-w64 i686 | 444 passed | 8 passed | - |
| MSVC x64 | 444 passed | 8 passed | all passed |
| Linux GCC x64 (WSL) | 444 passed | 8 passed | all passed |

The fix changes emulation output on purpose (the OS's ESP writes now wait for the PC, so they land in other
samples). New reference hashes, identical on the three toolchains:

| Hash | Before | After |
|---|---|---|
| Boot trace | `ba7e34a4cf23d1e0` | `9040ac63d78ec3c9` |
| Render `player_smoke.mid` | `23b6485893beb761` | `f9bfc97c7c662df9` |
| Render `full_chord.mid` | `0aa23b0ebbdeeec6` | `64367ff535b7b1c1` |
| Render `filtered_patch.mid` | `e9d27e034e29b527` | `b688d88b6ca141be` |

## Other sounds

A survey of all 120 U0 sounds (old and new build): a C+E chord held, the wheel swept 0 to 1023 in 1 s (faster
than the test), counting the OS's GPR read-backs that differ from what it wrote. Only six sounds have any:

| Sound | Before | After |
|---|---|---|
| U0-1 LATIN-PIANO (program 6) | 7 | 2 |
| U0-6 ROCK-ORGAN1 (36) | 4 | 7 |
| U0-6 CNCRT-ORGAN (40) | 0 | 1 |
| U0-7 ST-MARKS (45) | 15 | 7 |
| U0-2 WHL-DDL-BEL (74) | 10 | 2 |
| U0-5 RAIN-FOREST (91) | 0 | 1 |

36 before, 20 after. Each remaining one is the same race, but now only where the OS's 1 kHz DUART timer interrupt
runs between the read-back and the parking read (c4c9a8 enables interrupts first). That puts the parking read
at any point in the sample, and when the next GPR's data follows within the same sample, 410-430 CPU clocks
later, ZERO still overwrites it. The OS's PC wait cannot prevent this. Under the datasheet's model (ESP notes
part 2 §5.1.2: a read fills $00-$02 at the next END) a real TS-10 does the same, and the OS's check rewrites the
value 3 samples later. So a fast wheel sweep can still click now and then; a slow one (the test) does not.

## Open

- The remaining race after a timer interrupt (Other sounds). Removing it would need a model the datasheet does not
  describe, for example a read that leaves $00-$02 alone once the host has written them. That is not done
  without evidence from a real TS-10.
- Where in the frame the real ESP's END falls is not known; it is taken at the frame's end.
- `tsemu bench` (MSVC): realtime x3.79.

## User to run

- WHL-DDL-BEL (U0-2, the third sound): hold a note or chord and move the mod wheel up and down, slowly and fast,
  with the on-screen wheel and with a MIDI keyboard's wheel. The delay should move with no crackle.
- Fast wheel sweeps on ST-MARKS (U0-7), LATIN-PIANO (U0-1) and ROCK-ORGAN1 (U0-6): a rare click may remain
  (see Other sounds). Say whether it is often enough to need more than the hardware model gives.
- A few sounds without wheel effects (BABY-GRAND, a pad) still sound as before.
