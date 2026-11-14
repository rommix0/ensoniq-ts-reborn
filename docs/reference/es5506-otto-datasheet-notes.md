# ES5506 "OTTO" datasheet notes

Source: `datasheets/ES5506 OTTO Specification Rev 2.3.pdf` (51 pages; printed page P is PDF page
P+1). Citations are §section/p<printed page>. "BM" = the register bit-map charts, pp18-20 (no section
number, under §7). Tables, figures and garbled OCR values were read from page renders. "(inferred)"
marks what was not read directly. Ambiguities are listed in §9; each becomes a decision (`OT` ids).

## 1. Host interface

- Host bus HA[5:0] address, HD[7:0] data (§9.2/p22, §10.2/pp25-26). Each register is 32 bits: the
  upper four address lines pick the register, the two lowest pick the byte (§13/p38). Locations are
  written H00..H78 in steps of 8; the host diagrams wire CPU HA[6:1] to OTTO (§13.1/p40), so a
  location is a CPU byte offset (inferred). Byte 0 is the most significant byte (68000 order, §2/p5).
- **Write latching** (§13/p39): writes to bytes 0-2 go to temporary latches; writing byte 3 commits
  the whole 32-bit register.
- **Read latching** (§13/p39): reading byte 0 performs the 32-bit read and latches bytes 1-3; reads
  of bytes 1-3 return the latch.
- **PAGE** (§2.1/p5, BM p18): loc 78 in every page, bits 5:0 PS5..PS0, bit 6 TEST. 65 pages:
  00-3F plus page 40, reached with TEST=1; TEST opens the channel registers (§6/pp14-15).

| Loc | Reg | Pages 00-1F (voice = page) | Pages 20-3F (voice = page-20h) | Page 40 (TEST, write-only) |
|---|---|---|---|---|
| 00 | 0 | CR | CR (same register, §4.1/p8) | CH0L |
| 08 | 1 | FC | START | CH0R |
| 10 | 2 | LVOL | END | CH1L |
| 18 | 3 | LVRAMP | ACCUM | CH1R |
| 20 | 4 | RVOL | O4(n-1) | CH2L |
| 28 | 5 | RVRAMP | O3(n-1) | CH2R |
| 30 | 6 | ECOUNT | O3(n-2) | CH3L |
| 38 | 7 | K2 | O2(n-1) | CH3R |
| 40 | 8 | K2RAMP | O2(n-2) | CH4L |
| 48 | 9 | K1 | O1(n-1) | CH4R |
| 50 | 10 | K1RAMP | W_ST (common) | CH5L |
| 58 | 11 | ACTV (common) | W_END (common) | CH5R |
| 60 | 12 | MODE (common) | LR_END (common) | "EMPTY" |
| 68 | 13 | PAR (global) | PAR | PAR |
| 70 | 14 | IRQV (global) | IRQV | IRQV |
| 78 | 15 | PAGE (global) | PAGE | PAGE |

- Voice registers read and write all their bits (§4/p8). Channel registers are write-only (§6.1/p15).
  Unused bits, page-40 reads and EMPTY: datasheet silent.
- **Host access to sound memory** (§13/p39, §10.1-10.2/pp24-27): 16-bit transfers through external
  latches, done in free memory slots (every other memory cycle in a single-OTTO system). OTTO has no
  DMA address register: the address is the CPU address through external latches (p40). This is board
  wiring, not chip state.

## 2. Register bit layouts (BM pp18-19 unless noted)

- **CR** (16 bits, §4.1/p8, table p17): 15 BS1, 14 BS0, 13 CMPD, 12 CA2, 11 CA1, 10 CA0, 9 LP4,
  8 LP3, 7 IRQ, 6 DIR, 5 IRQE, 4 BLE, 3 LPE, 2 LEI, 1 STOP1 (CPU stop), 0 STOP0 (oscillator stop).
- **FC** [16:0]: I5..I0 [16:11], F10..F0 [10:0] = 6.11 (§4.5/p11).
- **START** [31:11] = A20..A0, no fraction (§4.2/p11). **END** [31:7] = A20..A0 + F10..F7 (§4.3/p11).
  **ACCUM** [31:0] = A20..A0 [31:11], F10..F0 [10:0] = 21.11 (§4.4/p11). All three share one layout.
- **LVOL/RVOL** (§4.6/pp11-12): EX3..0 [15:12], M7..M0 [11:4], VF3..0 [3:0] (fine bits for ramps).
  Only the 12 MSBs enter the multiply (p35).
- **LVRAMP/RVRAMP** [15:8]: SIGN, RM2..RM0, RF3..RF0; bits 7:0 not implemented (§4.7/p12).
- **ECOUNT** [8:0] (§4.10/p12).
- **K1/K2** (§4.11, §4.13/p13): FC11..FC0 [15:4] used by the filter, FCF3..0 [3:0] ramp fine bits.
- **K1RAMP/K2RAMP** [15:8] SIGN, RM2..0, RF3..0, plus SLOW at bit 0 (§4.12, §4.14/p13).
- **ACTV** [4:0]: voices = value + 1 (§3.1/p6).
- **MODE** [4:0] (§3.2/pp6-7, table p17): 0 LRCLK_EN, 1 WCLK_EN, 2 BCLK_EN (all active low: 0 = pin
  is an output), 3 MODE0 "MSTR", 4 MODE1 "DUAL". MODE1:MODE0 = 00 single master early address,
  01 single master normal, 10 dual slave, 11 dual master.
- **W_ST, W_END** [6:0]: BCLKs after LRCLK; **LR_END** [6:0]: LRCLK high width (§3.3-3.5/p8).
- **PAR** [9:0] (§2.3/p6). **IRQV**: V4..V0 [4:0], "I" at bit 7 mirrors the IRQB pin (§2.2/p5).
- **Filter temps** (§5/pp13-14, BM p19): 18 bits, SIGN SIGN [17:16], FD14..FD0 [15:1], FDF0 [0]
  (inferred: guard bit, 15 data bits, one fraction bit). Six registers: O4(n-1), O3(n-1), O3(n-2),
  O2(n-1), O2(n-2), O1(n-1). There is no O4(n-2).
- **CHxL/CHxR** (§6/p14, BM p20): 23 bits, G G G [22:20], SD19..SD0 [19:0].

## 3. Per-voice signal path

**Address generation** (§1/p3, §11.1/p29): the accumulator's 21-bit integer drives A0-A20, BS1:0
picks one of four 2-Mword banks. Algorithm, verbatim in substance: (1) integer on the bus (S1);
(2) add 1, put on the bus (S2), not kept; (3) add FC; (4) subtract Loop End; (5) "If Loop End >
Accumulator then add FC else add remainder of subtraction in Step 4 to Loop start". In reverse
(DIR=1) START and END swap roles (§11.1/p29, §4.1.7/p10). DIR=0 is forward (§4.1.5/p9). The compare
is strict: the accumulator may equal Loop End, so the sample at End+1 must equal the Start sample
(note p29); §4.1.7 says looping happens when the accumulator "exceeds" END.

| BLE | LPE | Mode (§4.1.7/p10) |
|---|---|---|
| 0 | 0 | No loop: stop at END, the ALU sets STOP0 (§4.1.10/p11) |
| 0 | 1 | Uni-directional loop |
| 1 | 0 | Transwave: at END jump to START plus the fraction, then the chip clears BLE and sets LEI and the voice runs on until the host rewrites END and the mode (§4.1.7-4.1.8/p10) |
| 1 | 1 | Bidirectional: the ALU toggles DIR at START/END (§4.1.5/p9) |

- **LEI**: the loop-end check result is ignored (§4.1.8/p10). **STOP1**: the ALU stores nothing back
  (§4.1.9/p10). STOP0 or STOP1 forces volume to 0 (§4.6/p12, §4.8/p12); the voice runs only when both
  are 0 (§4.1.10/p11).

**Sample formats** (§11.2/p30): linear 16-bit two's complement, or CMPD=1: 3-bit exponent in the
MSBs and 13-bit mantissa, decompressed before interpolation. Useful forms are 8 and 12 bits; the
system must force the unused low data lines to zero. Only the encoder is given: start with exp=7;
while bit15 equals bit14 shift left and decrement exp; then strip the sign bit and keep the next
bits; if exp reaches 0 keep the top bits without stripping the sign; code = exp<<5 | mant (8-bit
form). Table p31 examples: 7FFF→FF, 8000→E0, FFF0→1F, 00FF→0F. Decoder (inferred): exp≥1:
`(!m12)<<15 | m<<2`, arithmetic shift right by 7-exp; exp=0: `m<<3`, arithmetic shift right by 7.

**Interpolation** (§11.3/pp31-32): SF = S1 + ACCfr·(S2−S1), ACCfr = "the 9 fractional bits of the
Accumulator"; the interpolator output is 18 bits.

**Filter** (§11.4/pp32-34, §4.1.4/p9): four cascaded one-pole stages, results kept at 18 bits, 16
bits go from filter 4 to the pan stage. LP: `Yn = K(Xn − Yn−1) + Yn−1`. HP: `Yn = Xn − Xn−1 + K·Yn−1`.
Poles 1 and 2 are always LP with K1. Coefficients: `K_L = 65535·[...]`, `K_H = 65535·{cosW − 0.5 −
√(...)}` (the 0.5 caps one HP pole near 8 kHz); only the 12 MSBs of K are used (p35).

| LP4 LP3 | Filter 3, table p9 | Filter 3, diagram p34 | Filter 4 |
|---|---|---|---|
| 0 0 | HP, K2 | HP, K2 | HP, K2 |
| 0 1 | LP, K1 | LP, K1 | HP, K2 |
| 1 0 | LP, K2 | LP, K1 | LP, K2 |
| 1 1 | LP, K1 | LP, K2 | LP, K2 |

The ES5505 OTIS datasheet table (KS-32 `docs/reference/es5505-otis-datasheet-notes.md`, CR) is
identical to the p9 table. The p34 diagram shows the HP stage's Xn−1 from O2(n-2) (pole 3) or
O3(n-2) (pole 4) and Yn−1 from the stage's own O(n-1).

**Hardware envelopes** (§11.5/p35, §4.10/p12, §11.7/pp36-37):

```
if ECOUNT != 0 {
  LVOL += sx(LVRAMP); RVOL += sx(RVRAMP);
  if (K1SLOW == 0 || FILTCOUNT == 0) K1 += sx(K1RAMP);
  if (K1SLOW == 0 || FILTCOUNT == 0) K2 += sx(K2RAMP);   // printed K1SLOW on the K2 line
  ECOUNT--;
}
FILTCOUNT++
```

Additions are signed with saturation: no wrap past the maximum, no going below zero (p36). SLOW
updates the filter every 8th sample. ECOUNT is checked when the oscillator data is read and just
before the update; zero either time aborts the update (p35). With ECOUNT=0 the host may write VOL,
K1, K2 without them being overwritten (p12). Host sequence: ECOUNT=0, the four RAMPs, then ECOUNT
(p37).

**Volume and pan** (§11.6/p36): exponent/mantissa. Multiply the 16-bit signal by 1MMMMMMMM (9 bits),
take the 20 MSBs, shift right by 15 − exponent.

**Channels** (§11.6/p36, §6/p14, §12/p37): CA2..0 picks channel 0-5. 12 accumulators, 23 bits with 3
guard bits. At the end of the voice cycle each is latched into the shift register and cleared; the
lower 20 bits are sent, saturated to the rail on overflow.

## 4. Timing

- 16 CLKs per voice, 1 µs at 16 MHz (§3.1/p6). Fs = CLK / (16 × voices): 32 voices at 16 MHz =
  31.25 kHz; 24 voices at 16.9344 MHz = 44.1 kHz. At least 5 voices (§3.1/p6). CLK 8-16.9 MHz (§10/p23).
- BCLK = CLK/4, 4 BCLKs per voice (§10.3/p27). Voices run in succession (§1/p3); LRCLK asserts when
  the oscillator counter wraps to 0 (§12/p38).
- Serial (§12/p38, Fig 16.6/p48): a BCLK counter resets on both LRCLK edges; the data window runs from
  W_ST to W_END; LRCLK high = left (§10.3/p27); MSB first, first bit labelled "channel bit 20".
- A 3-voice pipeline can overwrite CPU writes to DIR, STOP0, LEI, IRQ, ACCUM, filter temps, volumes
  and K1/K2; write twice ≥3 µs apart (§4.1.9/p10, §8/p21).

## 5. Interrupts (§14/p41, §2.2/p5, §4.1.4/p9)

IRQE=1 and the accumulator reaches loop end (loop start when DIR=1), in any loop mode: CR.IRQ set,
the voice number latched into IRQV[4:0], IRQV bit 7 cleared, IRQB low. A host read of IRQV sets bit 7
and releases IRQB; the voice's CR.IRQ is cleared the next time it is processed. Other requesting
voices wait in their CR.IRQ; one is loaded into IRQV when it is next processed after the read. The CPU
can force an interrupt by setting IRQE and IRQ.

## 6. Pot A/D (§2.3/p6, §10.4/p28)

Reading PAR starts a conversion and sets PAR to 3FF. POT_RES discharges the capacitor for 4×1024 CLKs,
then a CLK/4 counter runs until POT_IN reaches about 2.8 V and the count is loaded into PAR. A
conversion takes 512 µs at 16 MHz. Use: read PAR, wait a timer tick, read again; each read returns
the previous conversion and starts the next.

## 7. Dual OTTO (§3.2/p7, §10.1-10.2, §13/p39)

Master/slave share memory on opposite E phases; host memory access in dual mode uses the master's
stopped-voice cycles. The TS has one OTTO; not modelled.

## 8. Reset state

ACTV "initializes to 32" (§3.1/p6), i.e. 1Fh (inferred). MODE: clock pins inputs, MODE1=1, MODE0=0
(17h, inferred) (§3.2/pp6-7). RESB also resets the internal clocks and oscillator counter (§10/p23).
Everything else: datasheet silent.

## 9. Ambiguities (decision candidates)

1. Filter 3 coefficient in modes 10 and 11: table p9 vs diagram p34.
2. DIR polarity: §4.1.5 says 0 = forward; the poorly printed BM header may read "1=FORWARD".
3. Loop compare: step 5 reads as ≥, the p29 note and §4.1.7 as strict >; FC appears to be added
   twice; the END fraction is not mentioned in the algorithm.
4. Reverse/bidirectional: FC subtracted when DIR=1?, S2 address in reverse, remainder at a
   bidirectional turn, accumulator after a one-shot stop, whether LEI suppresses the IRQ.
5. Which 9 of the 11 fraction bits are ACCfr; rounding.
6. The CMPD decoder (only the encoder is given); the table row "15⇒0, 00EF-0000" looks like a typo;
   what a stopped voice feeds the filter (§5/p14: address bus disabled, data "invalid").
7. Filter fixed point: K scaling; the HP "+0.5" read literally exceeds 1 for K above 7FFFh; which 16
   bits of O4 go to the pan stage; internal saturation.
8. Ramp alignment within VOL/K; the K2 line testing K1SLOW; FILTCOUNT width and scope; envelopes on
   stopped voices; saturation limits.
9. Volume: exponent 0; rounding; CA 6/7.
10. Serial: "channel bit 20" vs "lower 20 bits"; saturation width.
11. Host addressing: shared latches?; writing only byte 3; reads of unused bits and page 40.
12. Labels: the page-20h map says O3(n-1)/O2(n-1) where BM and §5 say (n-2); "fourteen" registers;
    IRQV "5 bits" with bit 7 used; IRQE called "IEB"; Fs printed with "+".
13. Pot: POT_RES "pulled low" vs "high active"; conversion on PAR read vs a free-running 2^13 clock.
14. Voice setup step 5: K1/K2 "FFFhex" (probably FFF0); §5 cites "Section 11.4" for §8.
15. TEST mode: does accumulation continue; pages 41h+; ACTV reset "32" in a 5-bit field.

## 10. OTIS vs OTTO

| | OTIS (ES5505) | OTTO (ES5506) |
|---|---|---|
| Registers | 16-bit, 16-bit bus | 32-bit, 8-bit bus with byte latches |
| PAGE | loc F, 128 pages | loc 78, 65 pages (TEST = page 40) |
| Accumulator / FC | 20.9 / 6.9 | 21.11 / 6.11 |
| START / END | 20.4 / 20.4 | 21.0 / 21.4 |
| Banks | 1 BS bit | 2 BS bits, 8 Mword |
| Samples | linear | linear or compressed (CMPD) |
| Filter temps / K | 16-bit / 12-bit | 18-bit / 12-bit + 4 fine |
| Volume | 4-bit exp + 4-bit mantissa | 4-bit exp + 8-bit mantissa + 4 fine, 9-bit multiplier |
| Envelopes | none | ECOUNT, four ramps, SLOW |
| Channels | 4, 16-bit | 6, 20-bit (23-bit accumulators) |
| Loop modes | no transwave | transwave (BLE=1, LPE=0) and LEI |
| Minimum voices | 8 | 5 |
| PAR | page 64+ | loc 68 in every page |
