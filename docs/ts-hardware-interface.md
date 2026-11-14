# TS-10 hardware interface (found by running the OS)

The facts the emulated TS-10 (`src/ts/machine.{h,cpp}`, `src/ts/fdc.h`) relies on. Each comes from
the schematic (plan 3 "Hardware facts"), the service manual, and running OS v3.10 and reading the bus
trace, the interrupt acknowledges, the exception counts and what the firmware sends on the DUART. The
firmware is run, never studied. Each section gives the fact, how it was found, the alternatives tried,
the confidence and the decision entry in `docs/decisions.md`.

**How the evidence was gathered (Plan 3 Task 4).**
- `tsemu boot --ms N --trace unmapped,iack,duart,otto,esp,fdc,scsi,iexp` on the machine, one change at
  a time.
- A scratch harness, not committed, linked against `ens_ts`. It printed a time line in ms: the first
  access of each kind (device, register, read/write, size), every exception with the PC it came from,
  each character sent on DUART A and B, and the ESP halt edges. Run-time switches: interrupt levels
  (`setIrqLevels`), DUART input pins (static or toggled), bytes queued on channel B RX.
- "Gets further" means: new kinds of device access, fewer unmapped accesses or fault vectors, and no
  "SYSTEM ERROR n" text on channel B. The display text is plain ASCII between control bytes; the
  protocol itself is Task 5's.

**Where the boot ends.** With the map below:
- The last new kind of device access comes at 8060 ms (the ESP is released at 8059.9 ms).
- The last display byte comes at 8076 ms. The display shows "CALIBRATING KEYBOARD - DO NOT TOUCH"
  from about 1.6 s and "KEYBOARD IS NOT RESPONDING" from 8.0 s; nothing changes in the 25 s after.
- Most of the time is two timeouts: about 0.9 s waiting for a floppy index pulse (empty drive), and
  about 5.5 s waiting for the keyboard microcontroller on channel B RX (Task 5).
- `kBootMs` = 12 200: 8076 ms plus 50 %, rounded up. The tests expect it to shrink once Task 5
  answers for the keyboard.

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

**Confidence.** Medium for channel B's rate (from the schematic's 1 MHz on IP5/IP6; the keyboard
replies in Task 5 will test RX). Low-medium for the CPU clock (unchanged from T1). **MAME.** Not
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

## Keyboard link (Task 5)

**Fact so far.** After "CALIBRATING KEYBOARD" the OS waits about 5.5 s for the keyboard
microcontroller on channel B RX (IMR has RxRDYB), pulsing SACK/ (OP7) once at 1033 ms, and then shows
"KEYBOARD IS NOT RESPONDING". Each of the 256 single bytes queued on channel B RX at 1100 ms is read by
the RxRDYB interrupt and changes nothing else. No stub is modelled: the boot goes on by itself after
the timeout. Task 5 finds the protocol.
