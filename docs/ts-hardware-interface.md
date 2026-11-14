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
- The last display byte comes at 3457 ms: the play screen ("Display and LEDs"). The ESP is released
  at 3357 ms; the last new kind of device access is at 1819 ms.
- About 0.9 s of it is the wait for a floppy index pulse (empty drive), 0.5 s the OS's pause before
  "CALIBRATING KEYBOARD" and 1.5 s the "ENSONIQ TS-10" start-up screen.
- `kBootMs` = 5200: 3457 ms plus 50 %, rounded up.
- Before Task 5 the boot ended at 8076 ms on "KEYBOARD IS NOT RESPONDING" after a 5.5 s wait for the
  keyboard, and `kBootMs` was 12 200.

## Memory map (T13, T4)

**Fact.**

| Addresses | What |
|---|---|
| 0x000000–0x03FFFF and 0xFC0000–0xFFFFFF | OSRAM, 256 KB (U3/U4), the same RAM in both windows. Reads of 0–7 come from the ROM until the first write into 0–7 |
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
- **OSRAM in two windows.** The reset SSP is 0xFF80A0. RAM use in the boot by 32 KB block: 0x0000–
  0x7FFF and 0xFF8000–0xFFFFFF heavily (from 0.2 ms), 0x8000–0x17FFF cleared at about 1.1 s,
  0x18000–0x1FFFF used from 1.27 s, and one word written and read back at 0x038000 at 1274.7 ms.
  0xFF8000–0xFF806D is never touched in the boot, so the 0x038000 probe does not disturb anything in
  the alias.

**Alternatives.**

| Candidate | Result |
|---|---|
| Task 3 map (64 KB at 0xFF0000, ROM readable at 0) | Rejected: stuck in the RAM loop at reset |
| 512 KB at 0, 0xFF0000 aliasing offset 0 | Rejected: at 203 ms a long read of 0x15001C (unmapped), a jump to 0, a run of illegal instructions, "SYSTEM ERROR 12" |
| 256 KB at 0, alias at 0xFC0000 | Chosen: boots |
| 128 KB at 0 (0x20000–0x3FFFF empty) | Also boots. The 0x038000 probe is then unmapped (reads 0); everything else is the same (event kinds and order identical, timing differs by a few µs) |
| 0xFC0000–0xFFFFFF a separate 256 KB | Also boots, with an identical event sequence. Rejected because the stack (0xFF80A0) and the vectors (0) must both be in RAM every TS has: U1/U2 (SEQEXP) are fitted only by the SQX-70 kit (service manual p59), so both windows are OSRAM |
| ROM only at 0xC00000–0xC7FFFF (no mirror) | Boots the same; the mirror is unobserved |
| Sample RAM: none, 2 MB, 2 MB aliased over the whole window, empty sockets reading 0xFFFF | All four boot with an identical event sequence; no "WRONG SOCKETS" text. The probe's result is not visible in the boot |

**Where SEQEXP is.** Not found. The boot touches no address outside the map above, so SEQEXP is
either probed later (the sequencer) or behind the 0x038000 probe. The target is a stock TS (no
SQX-70), so it is not modelled. A later check: the sequencer's free-memory display (30 000 notes stock,
97 000 with the SQX-70, service manual p59) with RAM placed at a candidate address.

**Confidence.** High for the device windows (each is the first place the OS goes, in the schematic's
decoder order, and nothing is unmapped). High for the OSRAM low window and overlay. Medium for the
top window being the same RAM (argued from the service manual, not observable). Medium for the
sample-RAM window base (a probe pattern); low for the installed size and empty-socket reads.
**MAME.** Not compared.

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
high, ESP halted), loads and reads back the ESP's GPRs and program while it is halted, and sets OPR
bit 6 at 8059.9 ms (ESP running). The ESP runs to the end of the boot.

**Alternatives.** The reverse polarity runs the ESP from 0.04 ms, during the OS's downloads, and leaves
it halted at the end of the boot. No error text either way.

**Confidence.** Medium.

## Analog reference (T8, provisional)

**Fact.** The reference input (mux Y7, select 7) must read non-zero: with every mux input at 0 the OS
reads PAR (OTTO register 13) and takes a divide by zero (vector 5) 10 µs later, at about 8.08 s, and
shows "SYSTEM ERROR 130". Reference values 1, 200, 800 and 1023 all boot. The machine uses 800 until
Task 6 finds the resting values.

**Confidence.** High that the reference must be non-zero; the value is a placeholder.

## Floppy controller (T10, provisional)

**Fact.** The uPD72069 sits at 0x280001 (A0 = 0: main status register) and 0x280003 (A0 = 1: data),
polled. The OS writes 0x36, 0x0B, 0x4F, 0xC8, 0xE3, 0x1E to the A0 = 0 address (not uPD765 commands;
ignored), and the uPD765 commands Sense Interrupt Status (0x08), Sense Drive Status (0x04 0x00) and
Specify (0x03 0xC2 0x11). It then polls the DUART IPCR for about 0.9 s for a change on IP0..IP3 (the
index pulse on IP0, DSTAT), and goes on without one (empty drive).

**How it was found.**
- Status reading 0: the OS polls the status for 34 ms, gives up and goes on.
- Status 0x80 always (RQM): the OS sends Sense Interrupt Status and waits about 0.9 s for a result.
- `src/ts/fdc.h`: RQM always, DIO and CB while result bytes wait, the uPD765 command lengths, and
  results for an empty drive (Sense Interrupt Status: ST0 0x80; Sense Drive Status: ST3 with only the
  drive bits; reads: not ready). The OS reads each result byte.
- Toggling IP0 every 30 frames ends the 0.9 s wait at once; toggling IP1, IP2 or IP3 does not. Static
  input levels (0x3F, 0x3E, 0x3D, 0x3B, 0x3C, 0x38) change nothing.

**Confidence.** Low: enough for the boot; Task 6 settles the empty-drive behaviour.

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
- **Boot end.** With the answer the last display byte comes at 3457 ms, the ESP is released at
  3357 ms and the last new kind of device access is at 1819 ms. `kBootMs` = 5200 (3457 ms + 50 %,
  rounded up).

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

**Open for Task 6.** With the provisional analog inputs (T8: every input 0 but the reference):
- the Volume slider at 0 silences the TS; `ts_key_sounds` raises it with `setAnalog`;
- the Data Entry slider at 0 keeps rewriting the selected parameter at its minimum (TUNE=-99,
  VEL-MAX=001), about one redraw per ms, which also delays what buttons show.
