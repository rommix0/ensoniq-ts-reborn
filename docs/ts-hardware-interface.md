# TS-10 hardware interface (found by running the OS)

The facts the emulated TS-10 (`src/ts/machine.{h,cpp}`, `src/ts/fdc.h`) relies on. Each comes from
the schematic (plan 3 "Hardware facts"), the service manual, and running OS v3.10 and reading the bus
trace, the interrupt acknowledges, the exception counts and what the firmware sends on the DUART. The
firmware is run, never studied. Each section gives the fact, how it was found, the alternatives tried,
the confidence and the decision entry in `docs/decisions.md`.

**How the evidence was gathered (Plan 3 Tasks 4 and 5).**
- `tsemu boot --ms N --trace unmapped,iack,duart,otto,esp,fdc,scsi,iexp` on the machine, one change at
  a time.
- A scratch harness, not committed, linked against `ens_ts`. It printed a time line in ms: the first
  access of each kind (device, register, read/write, size), every exception with the PC it came from,
  each character sent on DUART A and B, and the ESP halt edges. Run-time switches: interrupt levels
  (`setIrqLevels`), DUART input pins (static or toggled), bytes queued on channel B RX.
- "Gets further" means: new kinds of device access, fewer unmapped accesses or fault vectors, and no
  "SYSTEM ERROR n" text on channel B. The display text is plain ASCII between control bytes; the
  protocol itself is Task 5's.

**Where the boot ends.** With the map below and the keyboard's answer ("Keyboard link"):
- The last display byte comes at 3491 ms: the play screen ("Display and LEDs"). The ESP is released
  for the last time at 3376 ms; the last new kind of device access is at 1819 ms.
- About 0.9 s of it is the wait for a floppy index pulse (empty drive), 0.5 s the OS's pause before
  "CALIBRATING KEYBOARD" and 1.5 s the "ENSONIQ TS-10" start-up screen.
- `kBootMs` = 5300: 3491 ms plus 50 %, rounded up. With the Volume slider at 0 (Task 5's inputs) the
  boot ended at 3457 ms; at its resting maximum (T8) the OS spends 34 ms more before the play screen.
- Before Task 5 the boot ended at 8076 ms on "KEYBOARD IS NOT RESPONDING" after a 5.5 s wait for the
  keyboard, and `kBootMs` was 12 200.

## Memory map (T13, T4)

**Fact.**

| Addresses | What |
|---|---|
| 0x000000–0x037FFF and 0xFC0000–0xFFFFFF | OSRAM, 256 KB (U3/U4). The top window aliases all of it; the OS uses only its top 32 KB there (0xFF8000–0xFFFFFF, the stack), which the low window does not reach. Reads of 0–7 come from the ROM until the first write into 0–7 |
| 0x038000–0x077FFF | SEQEXP, 256 KB (U1/U2, the SQX-70 kit). Default: not fitted (a stock TS; reads 0, writes lost); `create(..., sqx70 = true)` fits it (Task 8) |
| 0x200000 + 0x40000 n | I/O1: n = 0 DUART, 1 SCSI, 2 FDC, 3 IEXP (A19:A18 into U7/A) |
| 0x300000–0x37FFFF | OTTO (odd bytes, HA5..HA0 = A6..A1) |
| 0x380000–0x3FFFFF | ESP (odd bytes, host register = A8..A1) |
| 0x400000–0xBFFFFF | Sample RAM host window: word (a − 0x400000) / 2 of OTTO banks 2–3 |
| 0xC00000–0xC7FFFF | OS ROM, 512 KB (T2); modelled mirrored to 0xCFFFFF |

The devices are decoded over their whole block (a DUART register is A4..A1 anywhere in
0x200000–0x23FFFF). Nothing else is touched in the boot: 0 unmapped accesses.

**How it was found.**
- **ROM, overlay.** On the Task 3 map the OS writes a long to 0 and 4 and compares, in a loop, right
  after reset. With RAM at 0 whose 0–7 read the ROM until written (the KS-32 K1 rule) the loop passes.
- **DUART.** The OS's first access is a byte write of 0x30 to 0x20001F (DUART register 15, reset OP
  bits: KRES and DRES). With the DUART at 0x200000 the OS sets up both channels and the timer and the
  display text arrives on channel B.
- **OTTO and ESP.** Next come byte accesses to 0x300001–0x30007F (64 odd bytes: the OTTO's 16
  registers x 4 bytes) and 0x380001–0x3801C1 (ESP host registers 0–0xE0). Both are I/O2, split by A19.
  At those places the OS's ESP GPR writes read back equal, the OTTO page register reads back, and the
  OS reads a running voice's ACCUM after a 10 ms DUART timer (its clock check).
- **SCSI, FDC, IEXP.** At 110 ms the OS writes and reads 0x240001/0x240003 (a probe that writes 0x55
  and reads back), then 0x280001/0x280003 (FDC); at about 1 s it reads 0x2C0001 once per 1 ms tick.
  These are I/O1 n = 1, 2, 3, exactly the U7/A order.
- **Sample RAM window.** At 107.5 ms the OS writes words to 0x400000, 0x600000, 0x800000, 0xA00000
  (0x0000, 0x1111, 0x2222, 0x3333 at +2) and reads them back from the top down: a socket probe in
  steps of 2 MB, one 1M x 8 SIMM pair (1M words). Four steps of 2 MB from 0x400000 end at the ROM:
  the 8 MB window of CPU A22..A1.
- **OSRAM in two windows.** (Task 4; the 0x038000 probe is SEQEXP's, below.) The reset SSP is 0xFF80A0.
  RAM use in the boot by 32 KB block: 0x0000–0x7FFF and 0xFF8000–0xFFFFFF heavily (from 0.2 ms), 0x8000–0x17FFF cleared at about 1.1 s,
  0x18000–0x1FFFF used from 1.27 s, and one word written and read back at 0x038000 at 1274.7 ms.
  0xFF8000–0xFF806D is never touched in the boot. (Task 4 took the 0x038000 probe for OSRAM; Task 8
  showed it is SEQEXP's, see below.)

**Alternatives.**

| Candidate | Result |
|---|---|
| Task 3 map (64 KB at 0xFF0000, ROM readable at 0) | Rejected: stuck in the RAM loop at reset |
| 512 KB at 0, 0xFF0000 aliasing offset 0 | Rejected: at 203 ms a long read of 0x15001C (unmapped), a jump to 0, a run of illegal instructions, "SYSTEM ERROR 12" |
| 256 KB: 0–0x37FFF plus the 0xFC0000 alias (SEQEXP split off in Task 8) | Chosen: boots, see "Where SEQEXP is" |
| 128 KB at 0 (0x20000–0x3FFFF empty) | Also boots (event kinds and order identical, timing differs by a few µs). Its unmapped 0x038000 is what a stock TS does, but the alias is then not the same RAM (Task 4 view, superseded by Task 8) |
| 0xFC0000–0xFFFFFF a separate 256 KB | Also boots, with an identical event sequence. Rejected because the stack (0xFF80A0) and the vectors (0) must both be in RAM every TS has: U1/U2 (SEQEXP) are fitted only by the SQX-70 kit (service manual p59), so both windows are OSRAM |
| ROM only at 0xC00000–0xC7FFFF (no mirror) | Boots the same; the mirror is unobserved |
| Sample RAM: none, 2 MB, 2 MB aliased over the whole window, empty sockets reading 0xFFFF | All four boot with an identical event sequence; no "WRONG SOCKETS" text. The probe's result is not visible in the boot |

**Where SEQEXP is (Task 8).** At 0x038000–0x077FFF, found by running the sequencer. A scratch build
(not committed) mapped RAM over all of 0x020000–0x1FFFFF and counted RAM writes per 32 KB block, the
PAL's granularity:
- The boot's only RAM access between 0x020000 and 0xFF8000 is the word written and read back at
  0x038000 (2 writes, 2 reads).
- The Sequencer Control page (pressed three times; Musician's Manual, "FREE") shows FREE=97311 when
  0x038000 holds what was written and FREE=31775 when it reads 0. 31 775 + 993 = 32 768 and
  97 311 + 993 = 98 304 events of 4 bytes: 128 KB and 384 KB of sequencer memory, the manual's 30 000 and
  "over 97,000 notes" with the SQX-70. The step is 256 KB, U1/U2's size.
- A sequence recorded with pressure (Record with Play, then keep it: YES) is written from the top of
  sequencer memory down: 0x068000–0x077FFF with the probe answered, 0x028000–0x037FFF without; the
  record buffer is at 0x018000–0x02FFFF either way. Nothing is written at 0x078000 or above.
So sequencer memory ends at 0x038000 on a stock TS and at 0x078000 with the SQX-70, and 0x038000–
0x03FFFF is never used through the low window. OSRAM therefore cannot answer at 0x038000 (a stock TS
would report the SQX-70), and the 32 KB that 0x038000–0x03FFFF would be in OSRAM are its top 32 KB,
seen at 0xFF8000. SEQEXP is the 256 KB from 0x038000.

The default machine is a stock TS-10 (design section 4 lists the SQX-70 as a non-goal): SEQEXP/ is
decoded but the sockets are empty, modelled as reads of 0 and lost writes, untraced (the board would
give open bus; 0 is enough for the probe to fail). The OS then shows FREE=31775 and keeps sequences in
OSRAM (ts_stock_sequence_is_kept_in_osram). `Machine::create(roms, err, true)` or `tsemu --sqx70` fits
the expander: FREE=97311 and sequences from 0x077FFF down (ts_sqx70_sequence_is_kept_in_seqexp). Before
Task 8 the low OSRAM window reached 0x03FFFF: the probe passed, the OS reported FREE=97311, and a kept
sequence went to unmapped space at 0x068000 up and was lost.

**Confidence.** High for the device windows (each is the first place the OS goes, in the schematic's
decoder order, and nothing is unmapped). High for the OSRAM low window and overlay. Medium-high for
SEQEXP's window (the probe, the free counts and where the data lands agree; the physical order of the
32 KB pieces inside U1/U2 is not observable and the image keeps CPU order). Medium for the
top window being the same RAM (argued from the service manual, not observable). Medium for the
sample-RAM window base (a probe pattern); low for the installed size and empty-socket reads.
**MAME.** Not compared.

## Battery RAM (T13)

**Fact.**
- The battery holds OSRAM and SEQEXP. `Machine::ramImage()` returns both as raw bytes, 512 KB, with or
  without the expander (the SEQEXP half is unused on a stock TS, so the file format does not change): OSRAM
  in CPU order (offset n is address n for n < 0x38000, and 0xFF8000 + (n − 0x38000) above), then SEQEXP
  in CPU order (offset 0x40000 + n is address 0x038000 + n).
- `Machine::loadRamImage(img, err)` takes exactly 524 288 bytes. A wrong size fails with "RAM image:
  expected 524288 bytes, got n" and changes nothing.
- Loading an image is a power cycle with the battery connected: the CPU, the OTTO, the ESP, the DUART,
  the FDC and the panel are reset, the 0–7 overlay comes back, the sample DRAM and the ESP delay DRAM (no battery) are cleared,
  and the analog and IEXP inputs keep their values (they are the player's controls). The next
  `runFrame` boots from the loaded RAM. A cold start is blank RAM, which is what `Machine::create` gives.
- **A boot from saved RAM is a warm start.** Master Tune set to +02 (System, Up twice) still reads
  "TUNE=+02" after a power-on from the image. The warm boot shows the cold boot's play screen (U0-0
  BABY-GRAND) with no warning or error text; its last display byte comes at 3014 ms against 3491 ms
  cold.
- **Garbage RAM is a cold start.** 512 KB of pseudo-random bytes give exactly the cold boot's display
  sequence, ending at 3491 ms on the play screen, and no fault vector: the OS rebuilds its RAM.
- A power cycle on a running machine (after a note left a tail in the ESP's DRAM) gives the same audio
  from power-on as a fresh machine loaded with the same image, and keeps the settings.
- `tsemu boot|render --ram <file>` loads the file at power-on if it exists and writes the RAM back at
  the end.

**How found.** ts_ram_image_keeps_settings, ts_garbage_ram_boots, ts_power_cycle_on_the_same_machine,
and `tsemu boot --ram` runs compared
with cold boots (display sequence, exception counts). The same runs give byte-identical image files
from the MinGW and MSVC builds.

**Alternatives.** OSRAM only (Task 4): loses the sequencer's SQX-70 memory, see "Memory map". Keeping the
sample DRAM or the ESP delay DRAM over a power cycle: not what the hardware does, and it would make a
load depend on the machine's earlier state.

**Confidence.** High for the warm and garbage behaviour (observed); the image layout is a choice (T13).

## OTTO voice count

**Fact.** The OS sets the OTTO to 32 voices: it writes 0x1F to ACTV (location 0x58, byte 3, bus address
0x30005F) with PAGE 0x00, twice: at 0.216 ms and again at 1027.6 ms, right after its DUART timer clock
check. Each time it first writes 0x11 to the same address under PAGE 0x20 (W_END there). ACTV stays
0x1F to the end of the boot.

**How found.** The bus trace of the OTTO's PAGE (0x30007F) and location 0x58 writes. The boot test
records the ACTV writes (location 0x58 byte 3 while PAGE is below 0x20) and checks the last value.
ACTV also resets to 0x1F, so `activeVoices() == 32` alone would not show that the OS wrote it.

**Alternatives.** None: this is observed, not chosen.

**Confidence.** High.

## How the OS drives the OTTO (OT1, OT7, OT16)

**Fact.** Every OTTO access is a byte access at an odd address. Reads are bytes 0-1-2-3 in order;
writes are 0-1-2-3, 2-3 (registers of 16 bits or fewer) or 3 alone (8 bits or fewer). The OS reads
no filter temps at boot and, while playing, only O1(n-1) of running voices; it writes the temps as 0.
It reads PAR about every 2 ms and changes the analog mux select within 127 slots after each read.
Counts and what they settle: `docs/decisions.md` OT1, OT7, OT9, OT10, OT12, OT13, OT16 and
`docs/verification-plan2.md`, "Left for Plan 3".

**How found.** A scratch harness (not committed) tallied the OTTO bus events of a boot and of one note
on each of the 60 bank programs plus a 32-note chord: access sizes, byte sequences per register, the
voice's CR at each filter-temp read (read from a copy of the OTTO, so the machine was untouched), PAR
reads against DUART output-port changes.

**Confidence.** High (observed).

## Interrupts (T5)

**Fact.** OTTO on level 1, DUART on level 3, all autovectored (AVEC). Level 2 (the disk line) has no
source. The OS writes IVR = 0x1B (27, the level-3 autovector number), unused under AVEC.

**How it was found.** Every placement of the DUART on levels 1–3 (with the OTTO on another level, or
on none) was booted. Only the DUART raises interrupts in the boot (the 1 ms tick and the channel B RX):
the OTTO never asserts IRQB, so its level is found from the OS's own handlers.

| OTTO, DUART | Result |
|---|---|
| 2, 3 / 1, 3 / none, 3 | Boots; 8143 acknowledges in 9 s, all level 3 (vector 27) |
| 3, 2 / 1, 2 | The first DUART interrupt (vector 26) gives "SYSTEM ERROR 139", then a reboot loop |
| 3, 1 / 2, 1 | The first DUART interrupt (vector 25) gives "SYSTEM ERROR 17" (unexpected OTTO interrupt), then a reboot loop |

So the level-1 autovector is the OS's OTTO handler, level 3 its DUART handler, and level 2 is not
expected (139). With the OTTO on level 1 and the DUART on 3 the boot is unchanged from the
placeholder (OTTO 2) because the OTTO never interrupts in the boot.

**Confidence.** High for the DUART (3). Medium-high for the OTTO (1): from the OS's error text, not
from an OTTO interrupt. **MAME.** Not compared.

## Clocks and the DUART serial clocks (T1)

**Fact.**
- Channel A (MIDI): CSRA = 0xEE, 16x external clocks IP3/IP4 at 500 kHz: 31 250 baud.
- Channel B (display and keyboard link): CSRB = 0xEE, TxCB = IP5 and RxCB = IP6, both 1 MHz:
  62 500 baud. The board's DUART is a 2681, which takes RxCB from IP6; the core (68681) takes RxCB from
  IP2, so the 1 MHz clock is given to the core's IP2 clock input. The IP2 level (DENS) is separate.
- The OS's clock check passes with the T1 clocks: a 10 ms DUART timer (ACR = 0x30, preload 0x09C4,
  X1/16), then a read of a running OTTO voice's ACCUM (0x0004A800).

**How it was found.**
- Without a clock on channel B the CSRB write counts as an unsupported write (D3) and the OS waits
  forever for TxRDYB (polling SRB at 0x200013). With 1 MHz on IP5 and IP6 the display text arrives.
- The X1 rate was varied in the scheduler's DUART accumulator, as seen from the CPU/OTTO clock:
  3.7, 4.3 and 4.7 MHz boot the same way (the ACCUM read changes: 0x00050800, 0x00045400,
  0x0003F800); 3.4 MHz (0x00057C00) stops sending display text during "CALIBRATING KEYBOARD". So the
  boot tolerates an X1 rate from -7 % to +17 % of 4 MHz and constrains the CPU clock only loosely.

**Confidence.** Medium-high for channel B's rate (from the schematic's 1 MHz on IP5/IP6; in Task 5
every byte the keyboard sends at this rate is read as sent). Low-medium for the CPU clock (unchanged from T1). **MAME.** Not
compared.

## ESPHALT (T7)

**Fact.** DUART OP6 is ESPHALT, active high, as on the KS-32: the OS resets OPR bit 6 at 1.3 ms (pin
high, ESP halted) and loads and reads back the ESP's GPRs and program while it is halted. It sets OPR
bit 6 (ESP running) at 1819 ms, halts the ESP again at 3301 ms, before the play screen, and releases
it at 3376 ms. The ESP runs to the end of the boot. (Measured with `tsemu boot --trace duart` on the
Task 6 machine; before the keyboard answered, Task 4 saw the release at 8.06 s.)

**Alternatives.** The reverse polarity runs the ESP from 0.04 ms, during the OS's downloads, and leaves
it halted at the end of the boot. No error text either way.

**Confidence.** Medium.

## Analog inputs (T8)

**Fact: the path.**
- The analog mux U69 puts one input on the OTTO POT pin, selected by DUART OP2..OP0. The OS reads
  the conversion as PAR (OTTO location 0x34, bytes 0x300069-0x30006F), the raw 10-bit value in the
  low bits.
- Every 2 ms the OS reads PAR, then writes OPR set = 0x07 (OP2..OP0 low) and OPR reset = n
  (OP2..OP0 = n). The PAR read starts the OTTO's conversion (OT16), which ends 512 slots (0.54 ms)
  later, after the new select: so each read returns the input selected 2 ms before. The mux switches
  at once in the model; any settling time under about 1.9 ms would read the same.
- Selects in the boot: 6 (battery) once at 1503 ms, 7 (reference) once at 1587 ms, 5 (data entry)
  at 1768 ms, 0 (pitch wheel) from 1843 ms, then the cycle 0 2 5 3 4 (10 ms) for good. The Analog
  Test page adds 7 and 6: 0 2 5 3 4 7 6 (14 ms). Select 1 (U69 Y1, not connected) is never used.

**Fact: the Analog Test page.** Hold Presets and press Compare (service manual p28). Two rows:
`ANALOG  PTCH=ppp MOD=mmm PED=ccc VOL=vvv` and `TESTS   KNOB=kkk REF=rrr BAT=bbb`. The readings are
filtered: after a step they take up to about 2 s to settle. Each select was set alone to a series of
raw values (the others at rest, REF 0x324) and the settled reading noted:

| Select | Input | Reading | Raw to reading (REF 0x324) |
|---|---|---|---|
| 0 | Pitch wheel | PTCH | Centred on the value seen at power-on (64 there). 0 at centre - 0x100, 24 at - 0x80, 93 at + 0x80, 127 at + 0x100. Further out the reading wraps (power-on 0, raw 0x300: 181). Powered on at raw 0 the centre reads 073 |
| 2 | Mod wheel | MOD | 0 up to 0x40, 16 at 0x80, 56 at 0x100, 96 at 0x180, 127 from 0x200 |
| 3 | Volume slider | VOL | 0 up to 0x20, 7 at 0x40, 47 at 0x100, 101 at 0x200, 127 from 0x280 |
| 4 | CV pedal | PED | 0 at 0, 16 at 0x40, 98 at 0x100, 127 from 0x180 |
| 5 | Data entry slider | KNOB | About (raw - 0x4A) / 2: 0 up to 0x4C, 127 at 0x14E, 255 from 0x24A. Shows 000 until the slider moves after the page opens |
| 6 | Battery | BAT | About raw x 0.234: 7 at 0x20, 119 at 0x200, 238 at 0x3FF |
| 7 | Reference | REF | raw / 4 - 1: 199 at 800, 200 at 804 |

- **The reference scales the others.** With REF at or below about 0x2D0 (reading 179) the readings
  above do not change (VOL 52 at raw 0x100 for REF 0x80-0x2D0). Above it they shrink by about
  750 / REF (VOL at raw 0x100: 48 at REF 0x320, 43 at 0x370, 36 at 0x3FF). REF 0 gives a divide by
  zero (vector 5, "SYSTEM ERROR 130").
- **Battery warning.** The OS reads the battery once in the boot, at 1503 ms. From raw 458 up the
  boot stops at "WARNING -- LOW BATTERY VOLTAGE / SAVE DATA - SEE USER MANUAL *CONTINUE*" (bisected;
  the same threshold at REF 0x2D0, 0x324 and 0x370). So a high reading is a bad battery, as the
  service manual says (p19, p28: a good unit shows BAT below 010).
- **Data entry at raw 0.** Exactly 0 is special: on a page with a selected value the OS keeps
  rewriting it at its minimum (TUNE=-99), about 600 panel bytes a second, and on the Analog Test page
  it shows "SYSTEM ERROR 144" about 5.4 s after the page opens. Raw 1 and above behave normally: no
  panel bytes at rest on the play screen or the System page.

**Resting values (T8).** `kAnalogRest` (`src/ts/machine.h`), set at power-on:

| Input | Raw | Reading | Basis |
|---|---|---|---|
| Pitch wheel | 0x200 | PTCH=064 | Centred: the power-on value reads 64. 0x200 leaves the wheel's full travel (+-0x100) on both sides without wrapping |
| Mod wheel | 0x000 | MOD=000 | Wheel down (service manual p28: down value 000) |
| Volume slider | 0x3FF | VOL=127 | Up, so a headless render is audible (at 0 the TS is silent) |
| CV pedal | 0x3FF | PED=127 | Unplugged: "Check that the display shows: PED=127" (service manual p28, before the pedal is plugged in) |
| Data entry | 0x14E | KNOB=127 | Middle of 0-255; not 0, which the OS takes as a slider held at its minimum |
| Battery | 0x000 | BAT=000 | A good battery reads below 010 (p28); far from the warning at 458 |
| Reference | 0x324 | REF=200 | The middle of the service manual's 180-220 (p28) |

`setAnalog` clamps to 0-1023 (a request beyond 10 bits stays the maximum) and ignores an input outside
the enum; `tsemu boot --analog NAME=raw` sets an input from power-on.

**How it was found.** A scratch harness, not committed, linked against `ens_ts`: the OTTO and DUART
accesses on the play screen and the Analog Test page (PAR reads and OPR writes, with times); one input
at a time stepped through raw values with the page open, the settled rows read (1.5-2.5 s per step);
a grid of REF values against fixed inputs; boots with the battery bisected for the warning; boots with
the pitch wheel at 0, 0x100, 0x200 and 0x300; panel bytes counted over 1 s for data entry values
0-0x3FF.

**Alternatives.** Before Task 6 every input but REF (800) was 0: PTCH=073, PED=000, VOL=000 (silent),
and the data entry flood above. Raising the mod wheel or the CV pedal at power-on changes nothing in
the boot but the readings.

**Confidence.** High for the path, the select order and the select table. Medium for the raw-to-reading
values (read off settled displays) and the reference scaling. Medium for the resting values: the real
voltages are unknown; each reads what the service manual expects.

**Tests.** ts_cold_boot_is_clean, ts_analog_resting_values, ts_slider_moves_volume, ts_key_sounds.

## IEXP port (T9)

**Fact.** IEXP (U12/A, a 74HC244) is read as a byte at 0x2C0001 once per 1 ms tick from about 1 s:
D0 FSW3B/, D1 FSW4B/, D2 PSRB/, D3 PSLB/, low = pressed. The OS ignores D4-D7: the boot, a page
change and a patch select press give the same bus trace (IEXP values left out) with the upper nibble
0xF, 0x0, 0x5, 0xA, 0x7 or 0xE. The model reads them high.
- **Patch select.** Left (D3) sends MIDI controller 70 = 0x40, right (D2) 0x20, both 0x7F, none 0x00
  (the Musician's Manual, "Using the Patch Select Buttons", names controller 70).
- **Foot Switch 2.** Its contacts are FSW3B/ and FSW4B/ (Foot Switch 1 goes through the keyboard,
  "Buttons"). With SW2-R=SUSTAIN on the second System page, D0 (FSW3B/) sends controller 64 and D1
  does not; with SW2-L=SUSTAIN as well both do. So `footswitch(3)` is the right contact and
  `footswitch(4)` the left, the same order as Foot Switch 1 (code 00 right, 01 left). Both are
  *UNUSED* by default.

**How it was found.** The scratch harness traced the IEXP reads and MIDI out while pressing each input
from the play screen and the System page, and compared trace hashes across upper nibbles.

**Confidence.** High for D0-D3 and the sides; high that D4-D7 are ignored (their real level is unknown).

**Tests.** ts_iexp_inputs, ts_patch_select_is_read, ts_footswitch2_is_read.

## Floppy controller and the empty drive (T10)

**Fact.** The uPD72069 sits at 0x280001 (A0 = 0) and 0x280003 (A0 = 1), polled. The drive is empty.
Register by register:
- **Main status register (read, A0 = 0).** RQM (0x80) always; CB (0x10) from a command's first byte
  to its last; DIO and CB (0xD0) while result bytes wait. The OS polls it before each byte.
- **A0 = 0 writes.** 0x36, 0x0B, 0x4F, 0xC8, 0xE3 at boot (110 ms), then 0x1E before each drive check
  and 0x0E after it (bit 4 set around the access: plausibly a motor or drive select bit of the 72069's
  own control register). They are not uPD765 commands; the model ignores them, and nothing the OS
  reads afterwards depends on them.
- **Data register (A0 = 1).** The OS uses Sense Interrupt Status (0x08; result ST0 0x80, no interrupt
  pending), Specify (0x03 0xC2 0x11; no result) and Sense Drive Status (0x04 0x00; result ST3 = the
  drive bits only: not ready, not write protected, not track 0, one side). Nothing else is sent with
  the drive empty.
- **DUART IP0 (DSTAT, the index pulse).** No pulse: no disk is turning. After each Sense Drive Status
  command the OS polls the IPCR 300 000 times (about 0.9 s at the modelled clock) for a change on
  IP0..IP3 before it reads ST3. A check that sees no change is "no disk".
- **DUART IP1 (DSKCHG) and IP2 (DENS).** Held high. No level of either changes anything the OS does
  with an empty drive (below), so they stay at the idle level of the other inputs. The schematic names
  them without a "/"; if active high, high reads as "disk changed" and "high density".
- **What the OS does with it.** At boot: one drive check (110-1017 ms), then on with no message. On a
  disk command (Storage twice: the disk page; or FORMAT): two drive checks, then "DISK DRIVE NOT
  READY" for about 1 s and the DISK page. The Musician's Manual gives that message as "No disk in the
  drive" (Storage, FORMAT DISK error messages).

**How it was found.** The scratch harness logged every FDC access and the IPCR reads in the boot, in
Storage-Storage and in FORMAT, and showed the settled display after each.

**Alternatives tried.**
- IP0 pulsing low for 2 ms every 200 ms (a turning disk): the boot's wait ends at once (play screen at
  2702 ms) and Storage-Storage shows "DISK DRIVE NOT RESPONDING": the OS then expects a drive that
  answers seeks and reads.
- Static input levels 0x3F, 0x3D, 0x3B, 0x39 (IP1 and IP2 each high or low), with and without the IP0
  pulses: the same messages at the same times.
- ST3 = drive bits | 0x00, 0x08, 0x10, 0x20, 0x40, 0x60 or 0x70: the same "DISK DRIVE NOT READY" (with
  pulses, "NOT RESPONDING") at the same times. The index pulse decides, not ST3.
- Sense Interrupt Status answering ST0 0xC0 and a PCN byte: the same messages.
- Task 4: a status of 0 makes the OS give up on the FDC after 34 ms; a static 0x80 makes it wait about
  0.9 s for a result.

**Confidence.** High that "no index pulse" is the empty drive and that the OS's answer is the manual's
no-disk message. Low for ST3, ST0 and the IP1/IP2 levels, which an empty drive cannot show.

**Tests.** ts_floppy_reports_no_disk, ts_cold_boot_is_clean.

## The ILLEGAL instructions at boot

**Fact.** At about 1017 ms (after the floppy wait) the OS executes two ILLEGAL instructions (0x4AFC),
17 µs apart, just before its DUART timer clock check. Its vector-4 handler steps over each one and the
boot goes on. They happen under every map, FDC and input variant tried, including the floppy failing
in a different way (at 144 ms when the FDC status reads 0). So they are the OS's own, not an emulation
fault. The boot test allows vector 4 only at an ILLEGAL opcode.

**Confidence.** Medium-high.

## Keyboard link (T19)

**Fact.**
- Channel B carries two directions. TxDB goes to the keypad/display board (the display and LEDs,
  below) and also carries the OS's commands for the keyboard, `FB nn`. RxDB comes from the keyboard
  microcontroller: its keys, its pressure sensors, the Foot Switch 1 jack and the panel buttons,
  which reach it from the display board (service manual p5-6, Figure 5).
- **KRES (OP4) and DRES (OP5) are active-high resets.** They are high from the DUART's reset (OPR
  = 0) until the OS sets OPR bits 4 and 5 at 0.24 ms, and stay low after that. With an active-low
  reset the keyboard would stay in reset from 0.24 ms on and could never answer.
- **SACK/ (OP7)** goes low at 1033.8 ms, just before the OS's first `FB FF`, and stays low in normal
  use. Under a continuous stream of key events at the line rate the OS pulses it high for about
  33 µs every few ms. The model reads it as "the keyboard may send while SACK/ is low": the keyboard
  puts its next byte on RxDB only when the line is idle and SACK/ is low.
- **Start-up.** At 1030.4 ms the OS reads RHRB 11 times (a flush), sends `E7` and `FB FF FB F0`,
  waits 500 ms, shows "CALIBRATING KEYBOARD - DO NOT TOUCH", then queues `FB FF FB FD` and waits about
  1.5 s for the keyboard. On a timeout it repeats `FB FF FB F0`, then 100 ms later `FB FF FB FD`, every
  1.6 s, four times in all, and then shows "KEYBOARD IS NOT RESPONDING".
- **The answer is one byte, `FF`.** It is taken from the moment the OS queues `FB FF FB FD` until its
  next `FB FF FB F0`. The OS then sends `FB FF FB F0`, 100 ms later `FB DD FB FC`, the "ENSONIQ TS-10"
  start-up screen, the 16 LEDs and the play screen. The model answers `FF` to `FB FD` at once.
- The OS sends `E7` and `FB FF FB F0` again before every disk access ("DISK DRIVE NOT READY" with the
  empty drive); keys and buttons work after it.
- **Boot end.** With the answer the last display byte came at 3457 ms (3491 ms since Task 6's resting
  analog values: "Where the boot ends"); the last new kind of device access is at 1819 ms.

**How found.** A scratch harness, not committed, logged channel B's bytes with their times, the
OP4/OP5/OP7 edges and the RHRB reads, and queued candidate bytes on RxDB.
- Every single byte 0x00-0xFF was queued at 1700 ms, during "CALIBRATING": only `FF` changes what
  the OS sends (the boot goes on to the play screen); the other 255 give the baseline stream.
- When `FF` is taken was bisected to 0.05 ms: from 1597.24, 3201.80 and 4806.60 ms. These are the
  moments the OS queues `FB FF FB FD`; the bytes themselves leave later (at 1608-1611 ms behind the
  calibration text, 1.4 ms later in the other rounds). `FF` at 1040-1590 ms, at 3200 ms and at
  4750-4800 ms (between an `F0` and its `FD`) is read and ignored. Task 4's sweep at 1100 ms fell
  outside the window, which is why it found nothing.
- At 1100 ms the bytes fall into two classes by the instructions the OS runs after them:
  0xC0-0xFF except 0xFC, and the rest. No byte of 0xC0-0xFE other than `FF` was found to do anything
  (pairs with a second byte 00 or 40 at 4000 ms change nothing visible).

**Alternatives.**
- A keyboard that sends `FF` by itself, once or repeatedly, would also boot if the byte fell in the
  window. A reply to the request is the simplest model that fits the window.
- A calibration delay before the reply is not modelled: the OS waits up to 1.5 s, and an immediate
  reply makes the boot shortest and deterministic.
- SACK/ ignored (as the KS-32 does): the boot and every test are unchanged, because the keyboard never
  has anything to send before SACK/ goes low. Flow control at byte level does not prevent overruns in a
  continuous line-rate flood (3172 overruns in 3000 pressure events with or without it), so its role
  is not settled.

**Confidence.** High for the `FF` answer and when it is taken; high for the reset polarity. Low for
SACK/'s role and for what `F0`, `DD`, `FC` and `E7` tell the keyboard. **MAME.** Not compared.

## Display and LEDs (T18, T15)

**Fact.**
- The display is 2 rows of 40 characters, positions 0-79 (row = position / 40). Bytes 0x00-0x7E are
  characters (only 0x20-0x7E were ever seen); 0x7F and 0x80 up are commands.
- **Steady state at `kBootMs`:** row 0 `U0-0 BABY-GRAND   FULL-BODY   BRITE-PNO `, row 1
  `     STEREO-PNO  WARM-GRAND  ALL-PURPOSE`, LEDs 0 (Bank 0) and 9 (Sounds). No column is
  underlined (the OS marks the selected sound only after a soft button selects one). Every byte of
  the boot is decoded. `ts_boot_shows_play_screen` pins this.
- The OS sends about one byte per ms.

**Command table.** "Effect" is what `Panel` does.

| Bytes | Effect | Evidence | Confidence |
|---|---|---|---|
| 0x00-0x7E | A character at the cursor; the cursor moves on, from column 39 of row 0 into row 1 and from 79 to 0 | All text; 32- and 34-character messages run on without a position | High for 0x20-0x7E; 0x00-0x1F never seen |
| 0x80 + p (0x80-0xCF) | Cursor to position p | `8D` "ENSONIQ  TS-10" (14 characters centred in 40), `AB` row 1; the sound names at 0x85, 0x91, 0x9D, 0xAD, 0xB9, 0xC5: three 11-character fields per row, one per soft button | High |
| `D6` | Blank the display and its attributes; cursor to 0 | Every page starts with it; text follows with no position ("U0-0") | High |
| `F5` / `F6` | Save the cursor / move it back there | "TUNE" `D1` "=" `F5 D3` "+00" `E8`, then many `F6 D3` "-99" `E8`: the value redrawn in place as the slider moves it. Without the restore the redraws run across the display | High |
| `D3` | Characters written from here on are underlined | Before the selected sound's name and the selected parameter's value, and only there: the manual's "the currently selected parameter is always underlined" | High |
| `D2` | ... have a flashing underline | Before the second sound after a double-click on its soft button, the primary one keeping `D3`: the manual's "the underline beneath the name of the layered sound will flash" (PDF p9) | Medium |
| `D0` | ... flash | `7F D0` "SAVE" `D1` when a Bank button is pressed on the Write page: the manual's "SAVE is flashing" (PDF p6 of section 8) | Medium |
| `D1` | ... are plain (no underline, no flash) | Before "=" and the value of every parameter. When another parameter is selected the old value is redrawn after `D1` with no `D6`, so `D1` must remove the underline | Medium-high |
| `E8` | ... keep the attributes their cells have | Ends every field. On the Write page's name editor the OS sets the cursor's underline with `D9`, then `E8`, then rewrites the letter under it every few ms; the underline must survive that (the manual: "a cursor, or underline, beneath the first letter") | Medium |
| `D9` / `DB` | Underline the cell at the cursor / remove it; the cursor stays | Name editor: `F6 D9` at the first letter; RIGHT gives `7F F6 DB 7F D4 7F D9 F5 F6` and the letter at the next position | Medium |
| `D4` / `D5` | Cursor right / left by one | RIGHT gives `D4`, LEFT the same sequence with `D5`. On the track pages each field is `D1 D5` " " `D3`/`7F` name: with `D5` moving left the 12-character fields start at columns 4, 16, 28 and end at 39; with `D5` taking a parameter or doing nothing they would overrun into "TRAX" and "STOP" | High for `D5`; medium for `D4` |
| `7F` | None | Comes before positions and modes (`D6 7F 82`, `7F D0`, `7F F6 DB`), and in the track lists where `D3` would mark the selected track. It takes no column (the 12-column track fields fit only so) | Low (effect unknown) |
| `E7` | None | Before each keyboard start-up: the boot and every disk access | Low (effect unknown) |
| `FF nn` | LED nn & 0x0F: lit if bit 7, flashing if bit 6 | Every page sends all 16, `FF 0F` down to `FF 00`, with bit 7 on the lit ones. `FF C9` (LED 9 lit and flashing) with Replace Track Sound: the manual's "the Sounds LED will be flashing" | High for lit; medium for flashing |
| `FB nn` | A command for the keyboard (Keyboard link); nothing on the display | `FB FF`, `FB F0`, `FB FD`, `FB DD`, `FB FC` only, always in pairs | High |
| other 0x80-0xFF | Counted in `PanelState::unknown`, no parameter | None seen in a sweep of every button code and every soft button on each page they open | - |

**LEDs.** 16, numbered by `FF nn`:

| LED | Control | LED | Control |
|---|---|---|---|
| 0 | Bank 0 | 8 | Bank 1 |
| 1 | Presets | 9 | Sounds |
| 2-5 | Bank 2, 4, 6, 8 | 10-13 | Bank 3, 5, 7, 9 |
| 6 | Replace Track Sound | 14 | Compare |
| 7 | Seq/Song Tracks 1-6 | 15 | Seq/Song Tracks 7-12 |

The Seqs/Songs LED is not on the link: it is DUART OP3 (SEQLED/, active low), which goes low when
Seqs/Songs is pressed and high again on Sounds.

**How found.** The bytes were read from the boot and from every page the buttons open (the scratch
harness above, then the `Panel` decoder itself: a sweep of all 64 codes, each followed by the six soft
buttons and Sounds, leaves `unknown` at 0). The meaning of each attribute byte comes from where the
OS puts it and the manual's description of that screen; whether "flash" means blinking text or a
blinking underline on the real display is not observable here.

**Alternatives.** `D1`, `E8` and `7F` all as "plain": the name editor's underline would vanish as
soon as the OS redraws the letter. `D5` with one parameter: the track fields overrun the display.
`D4`/`D9`/`DB` with a parameter: the name editor writes its letter one column left of the cursor.

**Confidence.** As per row. **MAME.** Not compared.

## Buttons (T16)

**Fact.**
- A press is `code | 0x80`, then `00`; a release is `code`, then `00`. Codes are 0x00-0x3F. A
  single byte does nothing.
- The 62 panel buttons and the two contacts of the Foot Switch 1 jack fill all 64 codes:

| Control | Code | Evidence |
|---|---|---|
| Bank 0-9 | 06 07 18 19 20 21 28 29 30 31 | In Sounds mode, from U0-5, Bank n shows bank page U0-n and lights its LED |
| Soft 1-3 (above the display) | 1E 26 2E | On a bank page, each selects the sound of its column on row 0 (MIDI program change 0, 1, 2; `D3` moves) |
| Soft 4-6 (below) | 1F 27 2F | The same for row 1 (programs 3, 4, 5) |
| Seqs/Songs | 0E | "SEQ0 $SONG-00 SEQUENCE-01 ..."; OP3 low |
| Presets | 0F | A preset bank page, "PSET" at the bottom left; LED 1 |
| Sounds | 10 | Back to the last sound bank page; LED 9 |
| BankSet | 11 | U0-5 to U1-5; held with Bank 2: R2-0 (the manual's direct dial) |
| Up / Down | 16 / 17 | TUNE +00 to +01 and back on the System page |
| Mix/Pan, Attack/Release, Brightness/Timbre, Key Zone/Velocity, Tuning | 36 3E 02 37 3F | Track pages "MIX", "ATCK", "BRIT", "ZONE", "XPOS" |
| Controllers On/Off, Performance Options, Track/MIDI, Track/Effects | 03 38 08 04 | "SUST" (ON per track), "PSEL" (patch select modes), "STAT" (SEND/RECV), "BUS" (VOICE) |
| Replace Track Sound | 39 | LED 6 lit, the Sounds LED (9) flashing: the manual's description |
| Seq/Song Tracks 1-6 / 7-12 | 09 / 05 | "TRAX" pages, the first with the sequence's track 1 (BEST-PAD), the second all undefined; LED 7 / 15 |
| System, Storage, MIDI Control | 0A 12 1A | "SYSTEM", "SELECT STORAGE OPTION", "MIDI" |
| Edit Song, Edit Sequence, Edit Track | 0B 13 1B | "SONG NOT SELECTED", "EDIT SEQUENCE", "EDIT TRACK" |
| Click, Sequencer Control, Locate | 0C 14 1C | "CLICK", "CONTROL", "LOCATE" |
| Record, Stop/Continue, Play | 0D 15 1D | Record held with Play: "REC" on the Locate page; Stop/Continue sends MIDI Stop (FC), Play MIDI Start (FA) |
| LFO, Env 1-3, Pitch, Pitch Mods, Filters, Output, Wave, Mod Mixer | 22 2A 32 3A 23 2B 33 3B 24 2C | Program pages "LFO", "ENV1"-"ENV3", "PITCH" (OCTAVE), "PITCH" (MODSRC), "FILTER1", "OUTPUT", "WAVE", "MOD MIXER" |
| Program Control, Program Effects, Select Voice, Copy | 34 3C 25 2D | "PROGRAM", "EFFECT", "SELV", "COPY" |
| Write (Write Program) | 35 | "WRITE EDIT PROGRAM NAME=..." with the name editor; LED 14 |
| Compare | 3D | LED 14 on, off on the next press, the page unchanged |
| Foot Switch 1 right / left | 00 / 01 | 00 sends MIDI controller 64 (127, then 0 on release) with the default FOOT-SW1-R=SUSTAIN; 01 does the same once FOOT-SW1-L is set to SUSTAIN on the second System page |

**How found.** From the play screen at 4000 ms, every first byte 0x00-0xFF was followed by `00`
and by `40`, and the display, LEDs and MIDI out of the next 300 ms were compared. With `00`, bytes
0x80-0xBF open pages, light LEDs or send MIDI (buttons); 0x00-0x3F with `00` do nothing (releases).
The codes that did nothing on the bank page were tried on the System page (Up/Down), with Record's
manual combination (hold, then Play) and on the System foot-switch page. Up/Down need the Data Entry
slider away from 0 (see "Open").

**Confidence.** High for the byte forms and the codes with a page or MIDI message; medium for
Compare (LED only) and the foot switch's left/right naming (from the System page's labels).
**MAME.** Not compared.

## Keys and pressure (T17)

**Fact.**
- Key down: `0x80 | k`, then the strike value (1-0x7F). Key up: `k`, then any nonzero byte; the OS
  sends note-off velocity 64 whatever it is. `k` = note - 36: the 61 keys C2-C7 are k = 0-60. A
  strike value of 0 makes the pair a button press.
- The strike value goes through the TOUCH table (VELTBL-4 by default). MIDI velocity out, white keys
  (C2, C4, C7) and black keys (C#4):

  | Strike | 1-8 | 16 | 24 | 32 | 40 | 48 | 56 | 64 | 72 | 80 | 88 | 96 | 104 | 112 | 120 | 127 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | White | 1 | 7 | 19 | 32 | 46 | 62 | 78 | 92 | 97 | 100 | 102 | 106 | 112 | 127 | 127 | 127 |
  | Black | 1 | 25 | 32 | 43 | 53 | 65 | 74 | 84 | 91 | 94 | 98 | 100 | 102 | 106 | 110 | 127 |

  `Panel::key` takes the strike value, clamped to 1-127. What it is physically (the keyboard times
  the gap between a key's two contacts, service manual p14) is not known.
- Poly-Key pressure: `0x40 | k`, then the amount (0-127). The OS sends it as MIDI polyphonic
  aftertouch (`A0 note amount`).
- Codes 61-63 (0xBD-0xBF, 0x7D-0x7F) play and send notes 97-99, but no key sends them.

**How found.** The sweep of "Buttons" with `40` as the second byte: 0x80-0xBF send note-on
`90 (code - 0x5C) vel` and 0x40-0x7F send `A0 (code - 0x1C) 40`. Key up and the velocity table by
sending the pairs one at a time and reading MIDI out.

**Open.** Pressure for k = 4, 12, 20, ... 60 (every eighth key) sent while the key is up gives no MIDI
out; with the key down it does. The other keys send pressure while up. Real pressure only comes with
a key down, so this does not affect the model.

**Confidence.** High for the byte forms, the note mapping and pressure. **MAME.** Not compared.

**Tests.** ts_panel_commands, ts_panel_button_bytes, ts_panel_button_table,
ts_panel_full_queue_drops_whole_events, ts_panel_lines (the model); ts_boot_shows_play_screen,
ts_button_changes_page, ts_key_sounds, ts_poly_pressure_is_sent (the firmware).

**Settled in Task 6.** With Task 4's analog inputs (every input 0 but the reference) the Volume slider
at 0 silenced the TS and the Data Entry slider at 0 kept rewriting the selected value at its minimum.
Both now rest elsewhere ("Analog inputs").

## MIDI (T20)

**Fact.**
- MIDI in and out are DUART channel A (`kMidiChannel`). The OS writes CSRA = 0xEE: both directions
  from the 500 kHz clocks on IP3/IP4, 31 250 baud, `rxBitClocks(0) == 128` after the boot. The boot
  sends 77 bytes on MIDI out, starting with a status byte (0xBB).
- `Machine::midiIn(byte)` queues a byte on RxDA (up to 1024, A1); it enters at the line rate, 320 us a
  byte (D2). `Machine::popMidiOut(b)` pops what left TxDA, oldest first (256 kept, A2).
- With the boot's defaults a note on channel 1 sounds: `90 3C 64` peaks at 2256 within 300 ms (3
  before). A burst of 33 note-ons and 33 note-offs queued at once gives no overrun and no warning text.
- Program change n selects sound n % 6 of bank n / 6 (`C0 05`: U0-0's ALL-PURPOSE; `C0 32`: U0-8's
  WOW-SWEEP; 0-59 tried). The play screen then underlines the selected sound.
- A panel pick sends its selection on MIDI out as `B0 00 00 20 ll C0 pp`: bank LSB `ll` 0 for the U0
  sounds and 2 for the ROM bank set R2, then the program change. Soft2 on the play screen sends
  `B0 00 00 20 00 C0 01`; BankSet twice, Bank2, Soft3 (R2-2) `B0 00 00 20 02 C0 0E`. Presets are
  programs 60 + 6 x bank + slot (slot 0-5 for Soft1-Soft6): Presets, Bank3, Soft4 sends
  `B0 00 00 20 00 C0 51` and row 1 starts `PSET`. The same bytes on MIDI in after a warm boot select
  the same sound again.
- The OS sometimes answers a MIDI-in program change with the same selection on MIDI out (`C0 05` in,
  `B0 00 00 20 00 C0 05` out). In the runs tried it did so for the first one or two after a boot and
  not for later ones; why is not known.
- A warm boot (the battery RAM kept) keeps the System settings but always starts on U0-0, as a cold
  boot does.

**How found.** The MIDI tests (`tests/test_ts_firmware.cpp`) and the scratch harness, sending each
program change and reading the display. The selection bytes by pressing the buttons through the
engine and reading MIDI out (`tests/test_app_engine.cpp`); the warm boot by loading a booted
machine's RAM image into a fresh one and reading its pages.

**Confidence.** High; low for when the OS answers a MIDI-in program change. **MAME.** Not compared.

**Tests.** ts_midi_rate, ts_midi_note_sounds, ts_midi_burst_no_overrun,
ts_program_change_changes_display, ts_render_is_deterministic, engine_tracks_panel_pick,
engine_restores_bank_and_preset, engine_restores_program_after_warm_boot, engine_program_seq_and_adopt.
