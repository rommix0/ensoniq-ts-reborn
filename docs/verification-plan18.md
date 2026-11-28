# Plan 18 verification: slow floppy load and the glide on release

2026-11-27, branch `plan18-disk-speed-and-voice-glitch`. Decisions T34 and T35. Two bugs the user reported after
playing version 1.1.0 with `DSKA0001_strings.hfe` (an HD HFE super disk of 255 cylinders, T30, with one sampled
sound, LOW STRING, 7771 blocks). Both were traced to the TS-10's own behaviour, not to the emulator. The user
chose to leave both as they are and write them down, so this plan changes only documents. No code changed and
no reference hash moved.

## 1. The load is slow (T34)

LOW STRING loads in 45.7 s of emulated time, from LOADING to the Load page.

- **Not the HFE reader.** The file decodes once, in 0.14 s, when it is opened. AD-001's GRND PIANO (a raw
  `.img`, 3055 blocks) loads at the same rate, 170 blocks a second (18.1 s).
- **Not the floppy controller.** The OS sends one Read Data command per sector, 5.8 ms apart. The controller
  has every byte ready at once (T22). The time is the OS's copy loop at c0f576-c0f5aa: 164 CPU clocks a byte,
  10.8 us at 15.24 MHz. These are the 68000's own instruction times.
- **Not the app.** The same load through `TsProcessor`, with 10 ms blocks and the 30 Hz tick (and so A129's disk
  copies), takes the same 45.7 s. On MSVC x64 Release that is 12.2 s of host time for the blocks and 0.6 s
  for all the ticks, 3.7 times real time.
- **A real TS-10 is slower.** An HD drive gives a byte every 16 us, so a sector takes at least 10 ms and this
  sound at least 80 s. With the turns lost at the track changes, 100-120 s.

## 2. A high sound when the second key is released (T35)

Reproduced with MIDI in: hold 60, play 67 at 800 ms, release it at 1600 ms. In the 50 ms after the release the
share of energy above 3 kHz goes from about 0.1 to 0.45. After that, one voice stays nearly stopped.

The cause is in OS v3.10:

1. Key 60 plays two voices. One of them is a mono layer with glide. On 67 it glides up a fifth, and that is
   correct.
2. On the release, the layer's note memory should glide back to 60. The code that does this (c13e50-c13ece)
   copies the held note's record without clearing bit 7 of its key byte.
3. The OS sets that bit on every note that comes in over MIDI (c0209c, c020d8, c085f8).
4. So the target is key 60 + 128. The pitch runs past the top of its range (the high sound), wraps negative
   and stays there.

Every other place in the OS that loads a key clears the bit, including the synth voices' copy of the same code
(c10964). The same notes from the TS's own keyboard glide back cleanly.

Which sounds are affected:

- Only sampled-sound layers in LEGATO or MINIMODE.
- Only notes from MIDI in: in the app, a MIDI keyboard or a DAW track. The on-screen keys go to the keyboard
  and are clean.
- Not ROM sounds: none of the 300 sounds in bank sets U0, U1, R2, R3 and R4 reach that code.

## Tests

No test was added, since nothing changed. The measurements were made in scratch tests that read the user's
image, which is not in ENS_DISK_DIR; they are not committed. T34 and T35 give the numbers.

## Toolchains

| | Core (`ens_tests`) | Real-time (`ens_rt_tests`) | App checks |
|---|---|---|---|
| MinGW-w64 i686 | 445 passed | 8 passed | - |

No source file changed, so MSVC, Linux and the app checks were not run again. The reference hashes are those of
Plan 17.

## Open

- The real drive's load time is worked out from the data rate and the track format, not timed on a TS-10.
- That a real TS-10 fed MIDI gives the same high sound follows from the code, but has not been tried on one.

## User to run

- With LOW STRING loaded, hold a key and press and release a second one, playing from the on-screen keys:
  the layer should glide back with no high sound.
- The same played from a MIDI keyboard gives the high sound. That is the OS's behaviour (T35).
