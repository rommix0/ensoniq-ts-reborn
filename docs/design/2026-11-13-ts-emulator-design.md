# TS Emulator and VST3 (TS-10 first): Design

Date: 2026-11-13
Status: approved by the user, 2026-11-13, with two changes: the name `ensoniq-ts-reborn`, and a
panel and keyboard layout close to the photo or the manuals.
Model: the user's KS-32 project, `C:\Users\abart\Desktop\Ensoniq KS-32` ("KS-32 Reborn", public at
github.com/rommix0/ensoniq-ks32-reborn). Its `AGENTS.md`, `README.md` and `docs/design/` are the
template for how this project is run.

How it was decided: the user chose the model (TS-10 first), the disk scope (a later stage) and the
code layout (a new repo seeded with copies of the KS-32 cores). Everything marked *my call* is the
designer's, open to correction.

## 1. Intent

Run the real Ensoniq TS-10 operating system on an emulated TS-10, inside a JUCE Standalone and VST3,
the way KS-32 Reborn does for the KS-32. The user plays the TS-10's own factory sounds and effects
from a MIDI keyboard or a DAW, and uses an on-screen front panel as they would use the instrument.

**Why.** Everything that makes a TS-10 sound and behave like one (programs, presets, TransWaves,
envelopes, modulation, the 24-track sequencer, the effects) lives in its firmware. Running that
firmware unmodified is the only faithful way to get it, and the KS-32 project proved the method.

## 2. Goals

1. **A new repo in this folder** (`C:\Users\abart\Desktop\Ensoniq TS`), seeded with copies of the
   KS-32's finished parts: the 68000 core, the ES5510 ESP core, the 68681 DUART, the trace format,
   the test registry, the tools that still apply, the real-time engine pattern and the JUCE app
   shell. KS-32 Reborn itself is not changed.
2. **An ES5506 "OTTO" core**, written from `datasheets/ES5506 OTTO Specification Rev 2.3.pdf`, the
   way the OTIS core was written from its datasheet: rule tests per register and per signal stage.
3. **A TS machine** (`src/ts`), built as a TS-10 in this stage: the 68000-class CPU, one OTTO, the ESP, the DUART, OS ROM, RAM,
   2 MB of sample RAM, the wave ROMs, the keyboard and keypad/display link, the analog inputs, and
   battery-backed memory, joined as the schematic wires them. It boots OS v3.10 to its play screen
   and plays factory sounds from MIDI.
4. **A JUCE app**, Standalone and VST3, Windows x64 (MSVC) first, Linux x64 as the KS-32 does: the
   whole TS-10 front panel on screen, firmware-driven, with plugin state and host automation on
   the KS-32 Plan 8 model.

## 3. What "passed" means

The KS-32's bar, carried over (*my call*, as the user has not restated it): the user starts the app
or loads the VST3, plays the TS-10's factory programs and presets from their MIDI keyboard, and uses
the on-screen panel as on the instrument. Supporting evidence:

- The firmware boots the same way every run; a headless run reaches the play screen with the
  expected display text.
- Rule tests for the OTTO core and for the machine behaviour the firmware relies on.
- Reference hashes (boot trace, render) printed and identical on all three toolchains: MSVC x64,
  MinGW-w64 i686 and GCC x64 on Linux, as KS-32 `AGENTS.md` requires. The core suite passes on MSVC
  and MinGW; the app builds on MSVC and Linux GCC.
- Every behaviour the documents leave open is logged in `docs/decisions.md`.

## 4. Non-goals (this stage)

- **The disk drive** (the user's choice: a later stage). The drive reads "No Disk". No disk images,
  no EPS/ASR sample loading from disk. The floppy controller is emulated only as far as the OS
  needs to boot and report an empty drive.
- **The TS-12.** Same boards, different OS EPROMs and a 76-key mono-pressure keyboard (service
  manual p5). Out of scope for this project (the user's decision, 2026-11-14).
- **SCSI**, the SQX-70 sequence expander, sample-RAM sizes above 2 MB.
- **MIDI out to the host.** As on the KS-32, the firmware's MIDI out is read and discarded.
- **Disassembling or mapping the firmware** (see anti-goals; lifted 2026-11-14).

## 5. Anti-goals: failure even if it "works"

Carried over from KS-32 `AGENTS.md`, unchanged:

- **Run the firmware unmodified.** The OS runs as-is from the user's ROMs. (Until 2026-11-14 this
  also said "never study it"; the user lifted that part so the ESP effect programs could be traced.
  Studying the firmware is now allowed as evidence.)
- **MAME is a comparison point, never a reference.** `docs/reference/mame/esqkt_TS_Work_without
  FX.cpp` (the user's working copy of MAME's driver) may be read to compare results, never copied
  to match. Note it is wrong in at least one place already: it maps one ES5506 at 0x300000 while the
  KT-76 notes in the same file describe two.
- **No JUCE, OS calls or file I/O under `src/`.** Real-time audio path: no allocation, lock or block
  after boot.
- **Silent guesses.** Every judgment call is a `docs/decisions.md` entry and a `// DECISION <id>`
  comment at the point of use.

## 6. Constraints

- Windows 10, no real TS-10 hardware: ground truth is the documents and the running firmware.
- C++17 and CMake; the core builds on MSVC x64, MinGW-w64 i686 and GCC x64 and stays bit-identical
  across them. JUCE 8 only under `app/`. Same as KS-32.
- AGPLv3, as KS-32 (JUCE is used under it).
- ROMs: see section 9.

## 7. Approaches considered

| | Approach | Trade-off |
|---|---|---|
| **A (chosen)** | New repo here, seeded with **copies** of the KS-32 cores and app shell | Fastest to a booting TS-10; nothing shipped is at risk. A fix in a shared core must be ported by hand. |
| B | Extract the shared cores into a library both projects use | One copy of each core, but means reworking a released, public repo and its pinned hashes first. |
| C | Add the TS-10 as a second machine inside the KS-32 repo | One codebase, but contradicts KS-32's own non-goal ("no other Ensoniq machines") and mixes two products' releases. |

The user picked A. B stays possible later if both instruments keep growing.

## 8. Shape of the system

Same layering as KS-32 (`README.md`, "How it is built"):

| Folder | What | Origin |
|---|---|---|
| `src/m68k` | 68000 core | copied as is |
| `src/esp` | ES5510 ESP | copied as is |
| `src/duart` | 68681 DUART | copied as is |
| `src/common`, `src/trace` | bit helpers, ring, trace format | copied as is |
| `src/otto` | ES5506 OTTO | **new**, from the datasheet |
| `src/ts` | the TS machine (TS-10 now), panel/keyboard link, analog inputs | **new**, modelled on `src/ks32` |
| `src/app` | real-time engine and panel layout | adapted from KS-32 |
| `app/` | JUCE Standalone + VST3 | adapted from KS-32 |
| `tools/` | `tsemu` (boot, render a MIDI file, live), trace tools | adapted |
| `tests/` | `check.h` registry, rule tests, firmware tests | registry copied; tests per new part |

*My call:* the KS-32's `src/otis` and `src/player` (the pre-firmware MIDI test player) are not
copied. OTIS code may be read as a guide for OTTO stages that the datasheets describe the same way,
but OTTO is its own core with its own tests.

**Names.** The repo is `ensoniq-ts-reborn` (the user's choice, so the TS-12 can follow: the user
understands the TS-12 as the same synthesizer underneath with some added features; the service
manual agrees: same boards, different OS EPROMs and keyboard). Following that, nothing is named
after the TS-10 alone: namespace `ens::ts`, machine `src/ts`, tool `tsemu`, and the TS-10/TS-12
difference is a model setting on the machine (only TS-10 is built now). *My call:* the app and
plugin are named "TS Reborn". A plugin's name and ID cannot change later without breaking saved DAW
projects, so it should not say "TS-10" if a TS-12 model is added to the same plugin. Decision-id
areas as KS-32, plus `OT` for OTTO and `T` for the TS machine.

## 9. Hardware facts and where they come from

From `datasheets/TS-10 Digital Board Schematic (4010020001 rev C).pdf`, sheet 3, and the service
manual:

- **CPU (U17):** 68000-class, 16-bit data bus, A1-A23, MODE pin tied high. Not a 68EC020 (that is
  the KT-76; MAME also uses a 68000 for the TS). The KS-32 68000 core is expected to run it
  unmodified. Clock from the schematic's crystals (builder to read).
- **OS ROM (U5, U6):** both sockets are 27C4001 (512 KB, A1-A18), h on D8-15, l on D0-7. In OS v3.x the l
  EPROM is a real 4 Mbit part and h a 2 Mbit part. The schematic shows A18 tied high, but in every
  512 KB l dump the reset vector and all version differences are in the lower 256 KB, and the upper half
  is identical across v3.05/v3.10 and TS-10/TS-12. *Decision T2:* the image is the h file (even bytes) and
  the first 256 KB of the l file (odd bytes), 512 KB in all; a 256 KB l file (v1.20, v2.02) is used whole.
- **RAM:** four 621001 (128 K x 8) SRAMs: U3/U4 on OSRAM/ (256 KB of OS RAM), U1/U2 on SEQEXP/
  (likely the sequence-expansion RAM). Battery-backed (+VB). Which of them the OS uses, and where,
  comes from the schematic decode and traces.
- **Sound (U22):** one ES5506 OTTO rev 2. Sound memory: wave ROMs U41, U43, U45, U46 on sheet 2,
  plus the SIMM sockets (sample RAM). How the four ROMs form 16-bit sample words (u43 looks like a
  nibble ROM; the MAME notes call it "4 bit of compressed data part") comes from the sheet 2 wiring.
- **ESP:** ES5510 with its delay DRAM (sheet 3).
- **DUART:** 68681. The keyboard and the keypad/display board are each their own microprocessor
  (service manual p5-6, Figure 5). Display data goes from the digital board straight to the display
  board (DISPOUT, over the 7-wire cable). Button presses go from the display board to the keyboard,
  which merges them with key events and sends both to the digital board (SERIN on the 20-pin
  ribbon). The emulated keyboard-plus-panel is the TS-10's counterpart of the KS-32 `src/ks32/panel`.
- **Finding the link protocol:** what the OS sends is read from traces; what the keyboard must send
  (velocity, pressure, button codes, any power-up handshake) is found by feeding the running OS
  candidate bytes and watching how it reacts. This is running the firmware, not studying it: the
  KS-32 found its panel link the same way (KS-32 `docs/ks32-hardware-interface.md`, injected-byte
  and candidate experiments).
- **Floppy controller:** a PC-style FDC (sheet 3; part not yet identified). Emulated only to "no
  disk" in this stage.
- **Analog inputs:** pitch/mod wheels, pedals, battery, read through OTTO's pot A/D (datasheet
  section 2.3, "PAR").
- **Outputs:** Main L/R and Aux L/R (Musician's Manual, "Connections").
- **Keyboard:** 61 keys, velocity and Poly-Key pressure.

Discovered facts go in `docs/ts-hardware-interface.md`, with how each was found, as the KS-32 does.

**ROMs.** Sorted in `roms/` (see `roms/README.txt`): the app needs OS v3.10 (`os/ts10_310h.bin`,
`os/ts10_310l.bin`) and the four wave ROMs in `waves/`. *My call, following KS-32:* those six files
are committed; the other OS versions stay local (git-ignored) for compatibility checks; `datasheets/`
is git-ignored. **The user should confirm this before anything is pushed to a public remote.**

## 10. App behaviour decided so far

All *my call*, on the KS-32 model unless the user says otherwise:

- **Outputs:** the VST3 has two stereo output buses, Main and Aux, matching the hardware. The
  Standalone mixes both to the device's stereo pair by default.
- **Poly-Key pressure:** MIDI polyphonic aftertouch drives each key's pressure. Channel aftertouch
  is applied to every held key.
- **Memory:** battery RAM saved to a file (Standalone) and in the DAW project (VST3), as KS-32 Plan 8.
  The 2 MB of sample RAM is not saved (it is volatile on the real unit).
- **Panel and keyboard layout (the user's rule, replacing the KS-32's "photo is orientation only"):**
  close to the instrument as shown in `datasheets/ts10-photo.webp`, or in the manuals where they
  show it. What the documents hold:
  - The service manual has no front-panel drawing (only board, block and exploded views).
  - The TS-10 Musician's Manual, page 3 (PDF page 33), "Front Panel Controls", draws the left
    part: Volume slider, Data Entry slider with Up/Down arrows, the display with its six soft
    buttons, the mode buttons (Seqs/Songs, Presets, Sounds), BankSet and the ten Bank buttons. Its
    following pages name every other button.
  - The photo shows the whole instrument: the disk drive at far left, Volume and Data Entry, the
    2x40 display with three soft buttons above and three below, the mode and Bank rows under it,
    then the Track Parameters, System/Storage/MIDI, Sequencer (Edit Song/Sequence/Track, Click,
    Sequencer Control, Locate, Record/Stop/Play, Replace Track Sound, 1-6/7-12) and Programming
    (LFO, Env 1-3, Pitch, Pitch Mods, Filters, Output, Wave, Mod Mixer, Program Control, Program
    Effects, Select Voice, Copy, Write, Compare) groups, the TS-10 logo, the 61-key keyboard and
    the two wheels at left with the two Patch Select buttons above them.
  So: the photo sets the overall arrangement and proportions; the manual's page 3 drawing and its
  button names set the left section and the labels. Where they disagree, the manual wins on labels
  and the photo on placement.
- **OS version:** v3.10. Other versions should boot, but only v3.10 is tested.

## 11. Order of work (proposed plans)

1. **Seed the repo.** `git init`; copy the cores, tests, tools and build files; drop the KS-32-only
   parts; `AGENTS.md` and `README.md` for the TS-10. Done when the copied test suites pass on both
   toolchains.
2. **OTTO core** from the datasheet, with rule tests. Done when every register and signal stage the
   datasheet specifies has a test.
3. **TS machine (TS-10 model).** Address map, interrupts, the keyboard/panel link, analog inputs,
   FDC stub, ESP host. Done when `tsemu` boots OS v3.10 headless to the play screen with the expected display
   text and renders a MIDI file to WAV with a stable hash on both toolchains.
4. **JUCE app**, Standalone and VST3: the TS-10 panel, MIDI, memory, two output buses, plugin state
   and automation. Done when the user passes it by playing it (section 3).
5. **Floppy drive.** The uPD72069 and a disk in the drive, disk images, the drive on the panel
   (`docs/design/2026-11-14-ts-floppy-design.md`). Done when the OS loads an instrument from a user's
   image and saves to a blank one.
6. **Release folder** (`tools/dist.ps1`, `tools/dist.sh`), as KS-32 Plan 8. Done in Plan 6 (2026-11-15, design `2026-11-15-ts-release-design.md`): version 1.0.0, the six ROMs beside the program and inside the VST3 package (A125, A126).

Later stages: SCSI. The TS-12 is out of scope (section 2).

## 12. Left to the builder

- Clock rates, the address map, IRQ levels and vectors, and the ESP delay-RAM size, from the
  schematic and bus traces (each logged as a decision).
- The keyboard/panel serial protocol, from traces of the running OS. MAME's `esqpanel` may be used
  for comparison only.
- The wave-ROM bank layout from the sheet 2 wiring.
- The OTTO output rate and its relation to the number of active voices (datasheet section 3), and
  the engine's resampling to the host rate.
- Which Musician's Manual figure is the authoritative panel layout.
