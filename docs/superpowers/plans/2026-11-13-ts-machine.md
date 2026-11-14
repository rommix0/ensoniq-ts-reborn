# TS Machine (Plan 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A TS-10 machine (`src/ts`) that runs OS v3.10 unmodified on the copied 68000, ESP and DUART cores and the Plan 2 OTTO core, boots headless to the play screen, plays notes from MIDI, and a `tsemu` tool that boots and renders MIDI to WAV with hashes identical on MSVC x64 and MinGW i686.

**Architecture:** The KS-32 Reborn machine pattern (`KS = C:\Users\abart\Desktop\Ensoniq KS-32`; read `KS/src/ks32/*`, `KS/tools/ks32emu/main.cpp`, `KS/tests/ks32_firmware.h`, `KS/docs/ks32-hardware-interface.md`): a `RomSet` of file images, a `Board` owning the sound chips, a `Machine : m68k::Bus` with a per-voice-slot scheduler, a `BusEvent` trace, and a panel model on DUART channel B. What the schematic does not settle (the PAL equations behind the address map and interrupt levels, the panel protocol) is found by running the firmware and watching it, exactly as the KS-32 did.

**Tech Stack:** C++17, CMake, `tests/check.h`; MinGW-w64 i686 + Ninja and MSVC x64.

**Spec:** `docs/design/2026-11-13-ts-emulator-design.md` (section 11 item 3; sections 9-10). Board facts: the schematic (`datasheets/TS-10 Digital Board Schematic (4010020001 rev C).pdf`, sheets 1-3) and the service manual, as summarised in **Hardware facts** below. Chip behaviour: `docs/reference/es5506-otto-datasheet-notes.md`, the ESP notes, the DUART decisions D1-D7.

**Branch:** `plan3-ts-machine` from `main`; merged with a merge commit at the end.

## Global Constraints

- **Run the firmware; never study it.** No disassembly, decompiling, annotation or mapping of the OS. The `m68k` disassembler ring is only for seeing where the emulator hangs. No traces or listings of the firmware are committed. Byte statistics of the ROM files (sizes, fill, version differences) are allowed.
- **MAME is a comparison point, never a reference.** `docs/reference/mame/` may be read only to compare a finished result; any MAME value cited is marked "MAME, comparison only".
- No JUCE, OS calls or file I/O under `src/`; file I/O in `tools/` and `tests/` only. Integer-only emulation; output bit-identical on MSVC x64 and MinGW i686.
- Real-time shape from the start: no allocation after `create()` in `Machine::runFrame` and below; queues are fixed rings (`src/common/ring.h`). (The allocation counter test arrives with the engine in Plan 4.)
- Every judgement call: `// DECISION T<n>: …` at the point of use plus a `## T<n>:` entry under `## TS machine` in `docs/decisions.md`. Every discovered hardware fact: an entry in `docs/ts-hardware-interface.md` with Fact / How found / Alternatives tried / Confidence (the KS-32 file's format).
- Firmware tests read the ROMs from `ENS_ROM_DIR` (default `roms/`). The suite shares booted machines through function-local statics (KS-32 `bootedKs32`) to keep run time down.
- Hashes are printed, never asserted against constants; they are compared by hand across toolchains and recorded in the verification report.

## Hardware facts (from the schematic and service manual; sheet:location)

- **CPU** U17 MC68EC000, MODE high (16-bit bus). IPL2 tied high, so **levels 1-3 only**; IPL0/IPL1 come from PAL U67 fed by DUIRQ/ (DUART), OIRQ/ (OTTO) and DISKIRQ/ (pull-up only; the FDC INT pin is not connected). **AVEC** from U67: all interrupts autovectored. BERR never asserted. HALT and RESET tied to BRES/.
- **Clocks** (sheet 1): XTAL1 "30.?MHz" (digit unreadable) / 2 → CPU CLK and OTTO CLK (same net). XTAL2 18.432 MHz / 2 → ESP clock 9.216 MHz. XTAL3 16 MHz → FDC; /4 = 4 MHz DUART X1 (inferred); 1 MHz → DUART IP5/IP6; 500 kHz → IP3/IP4.
- **Decode** (sheet 3): PAL U68 (inputs A15-A23, AS/, FC0-2) gives DRAMROM/, OSRAM/, I/O2/, I/O1/, ?RAM/ (to OTTO), SEQEXP/, OSROM/, SUPER/. 74HC139 U7/A on I/O1/ by A19:A18 → 0 DUART/, 1 SCSI/, 2 FDC/, 3 IEXPCS/. U7/B on I/O2/ by A19 and LDS/ → OTTOCS/ (A19=0), ESPCS/ (A19=1): OTTO and ESP on odd bytes. No address ranges are printed.
- **DTACK:** ROM/RAM and ESP immediate; I/O1 devices 3 clocks late; OTTO through its own DTACK, registered once.
- **OS EPROMs** U5 (D0-7, the "l" file) and U6 (D8-15, "h") are 27C4001 sockets addressed by CPU A1-A18: a 512 KB window of 256K words. The schematic shows EPROM A18 tied high; the ROM files say otherwise (see T2).
- **RAM:** U1-U4 621001 (128K x 8), battery-backed (+VB): U3/U4 on OSRAM/ (256 KB), U1/U2 on SEQEXP/ (256 KB).
- **DUART** U10 2681: registers A4..A1, odd bytes. A = MIDI (in via 6N138, out, thru). B: TX → display (BSEROUT, J3-2); RX ← keyboard (SERIN, J4-17). OP0-2 = analog mux select AN0-2; OP3 SEQLED/; OP4 KRES (keyboard reset); OP5 DRES (display reset); OP6 ESPHALT; OP7 SACK/ (to the keyboard). IP0 DSTAT (disk index), IP1 DSKCHG, IP2 DENS, IP3/IP4 500 kHz, IP5/IP6 1 MHz.
- **Link topology** (service manual p5-6, Figure 5; J4-19 → J3-1 pass-through): the CPU sends display data straight to the display board; the display board's buttons go to the keyboard microcontroller, which merges them with keys into the CPU's channel B RX.
- **Analog mux** U69 74HC4051 → OTTO POT_IN: Y0 pitch wheel, Y1 n.c., Y2 mod wheel, Y3 volume slider, Y4 CV pedal, Y5 data entry slider, Y6 battery, Y7 reference. Service manual Analog Test: REF reads 180-220.
- **IEXP port** (U12/A 74HC244, read-only, D0-D3): D0 FSW3B/, D1 FSW4B/, D2 PSRB/ (patch select right), D3 PSLB/ (patch select left), active low.
- **OTTO** U22 rev 2: host HA5..0 = CPU A6..A1, HD = D0-7. SER0-2 re-clocked to ESP SER0, SER2, SER3 (inputs); SER3 straight to the analog board DAC; SER4/5 unused. IRQ → OIRQ/.
- **Sound memory** (sheet 2): bank 0 = U41 (901401, 2 MB, byte mode) on D15-8 plus U43 (901301, 1 MB) nibbles on D7-4 through a 74HC257 selected by SAD20 (0 = low nibble), D3-0 forced 0: 12-bit samples. Bank 1 = U45 (901402, word mode) where SAD20 = 0; U46 (901302, word mode) where SAD20 = 1, SAD19 = 0; U47 "EXPANSION" (empty) where SAD20 = SAD19 = 1. Banks 2-3: SIMM DRAM. The CPU sees SIMM DRAM directly through a host window (CPU A1-A22, up to 8 MB). Service manual: a 1M x 8 pair = one bank set, 1M words; a sampled-sound load costs one voice for DRAM refresh.
- **ESP** U27 rev 6: clock 9.216 MHz, DRAM TC511664 (64K x 16), host access byte-wide. SER0, SER2, SER3 ← OTTO SER0-2; SER1 → DAC. HALT = NAND(OLRCLK, NOT ESPHALT).
- **FDC** U20 µPD72069 ("72069"): CS FDC/, A0 = CPU A1, polled (INT n.c.).
- **SCSI** J9: no board fitted in the target configuration.
- **Reset:** MAX707; BRES (DUART, FDC) is power-on only; BRES/ (CPU RESET/HALT, OTTO, SCSI, analog board) is also driven by the RESET instruction.

## Decision Defaults

| Id | Topic | Default | Evidence / confidence |
|---|---|---|---|
| T1 | Clocks | CPU = OTTO = 15 238 090 Hz (30.47618 MHz / 2, the KS-32's crystal; the schematic's digit is unreadable), so a 32-voice frame is 512 CPU clocks, 29 761.9 Hz. ESP 9 216 000 Hz. DUART X1 4 MHz; IP3/IP4 500 kHz; IP5/IP6 1 MHz. FDC 16 MHz. | Schematic sheet 1. CPU clock: low-medium until the firmware's own timing checks pass (Task 4). |
| T2 | OS ROM image | Even bytes from the h file, odd bytes from the **first** 256 KB of the l file: a 512 KB image. A 256 KB l file (v1.20, v2.02) is used whole. | The schematic shows EPROM A18 tied high (upper half), but in every 512 KB l dump the reset vector (SSP 0xFF80A0, PC 0xC0073E) and all version differences are in the lower half; the upper half is identical in TS-10 3.05/3.10 and TS-12 3.05/3.10 and 69 % 0xFF. The dumps win. Medium-high. |
| T3 | Wave memory | Bank 0 word = `u41[a] << 8 \| nib << 4`, `nib = (a & 0x100000) ? u43[a & 0xFFFFF] >> 4 : u43[a & 0xFFFFF] & 0xF`, `a` = 21-bit address. Bank 1: `a < 0x100000` → U45 word `a`; `0x100000 ≤ a < 0x180000` → U46 word `a - 0x100000`; above → 0 (empty socket). Word byte order of U45/U46 from Task 1's smoothness test. Banks 2-3: sample RAM (T4). | Schematic sheet 2. Byte order: by measurement. Medium. |
| T4 | Sample RAM | 2 MB installed (one 1M x 8 pair): OTTO bank 2 words 0-0xFFFFF; bank 3 and the rest of bank 2 read 0 (no DRAM). The CPU's host window base and the SIMM probe behaviour are found from the firmware (Task 4); a "WRONG SOCKETS" message means the model is wrong. | Design section 4; service manual SIMM text. Medium. |
| T5 | Interrupts | Autovector for every level. Which of DUART/OTTO/disk is on level 1, 2 or 3 is found in Task 4 (KS-32 method: try each, SR-mask histogram, firmware error codes 17, 137, 138, 145). | Schematic (levels 1-3, AVEC). Assignment: by experiment. |
| T6 | Wait states | Not modelled unless a firmware timing check needs them (then: I/O1 devices +3 CPU clocks, OTTO +2). | Schematic DTACK chain. Low. |
| T7 | DUART wiring | As **Hardware facts**. OP pins are read as the inverted OPR (the core's `outputPins()`). | Schematic. High. |
| T8 | Analog inputs | Mux per **Hardware facts**; resting raw values found in Task 6 (wheels centred/zero, sliders mid/max, battery nominal, REF in the 180-220 reading). | Schematic, service manual. Values: by experiment. |
| T9 | IEXP port | Reads `0xF0 \| (~active & 0x0F)`-style: D0-D3 low when the footswitch/patch button is pressed, upper bits as found in Task 6. | Schematic. Medium. |
| T10 | Floppy | Drive empty: DSKCHG and DENS at the levels the firmware takes as "no disk"; the FDC answers as found in Task 6 ("No Disk" on screen, no error). | Design non-goal. By experiment. |
| T11 | Audio routing | OTTO ch0-2 → ESP SER0, SER2, SER3 as `sat16(v20 >> 4)`. Main L/R = ESP SER1; Aux L/R = OTTO ch3 as `sat16(v20 >> 4)`. ch4/5 dropped. ESP `advance(103)` per frame (9 216 000 / 3 / 29 761.9 = 103.2, E30's 3 clocks per instruction). | Schematic sheets 1, 3. Bit alignment: medium-low (check levels by ear in Plan 4). |
| T12 | SCSI and unmapped space | SCSI region and unmapped reads return 0 and are traced, unless the firmware shows it needs open-bus 0xFF (Task 4). | No board fitted. Medium. |
| T13 | RAM | OSRAM 256 KB and SEQEXP 256 KB, both battery-backed; bases, mirrors and any reset overlay found in Task 4. The battery image is both, OSRAM first. | Schematic. Bases: by experiment. |
| T14 | RESET instruction | Resets the OTTO only (BRES/); the DUART and FDC (BRES) are untouched. | Schematic sheet 1. Medium. |

## Review Focus

1. **A ROM file of the wrong size** (e.g. a 512 KB h file, a truncated wave ROM). `create` returns null with the file named; never a half-built machine. Task 1: `ts_roms_wrong_size_named`.
2. **Two machines in one process.** No shared or static mutable state: two machines booted side by side give the same trace hash as one alone. Task 4: `ts_two_machines_are_independent`.
3. **A frame with no CPU progress** (an instruction overshoot ≥ 16 clocks, or the CPU stopped). The scheduler must not stall or drift: 100 frames take 51 200 ± 60 CPU cycles whatever the instruction mix. Task 3: `ts_frame_cadence`.
4. **MIDI arriving faster than 31 250 baud allows** (a burst from a host). No overrun on channel A and no firmware "out of buffers" error. Task 7: `ts_midi_burst_no_overrun`.
5. **A power-on with RAM left over from another OS version or garbage.** The machine boots (the firmware reinitialises or ignores it) and never faults. Task 8: `ts_garbage_ram_boots`.

---

### Task 1: ROM set, OS image and wave memory

**Files:**
- Create: `src/ts/roms.h`, `src/ts/roms.cpp`, `tests/test_ts_roms.cpp`, `tests/ts_firmware.h` (the ROM loader for tests: reads `ENS_ROM_DIR` files into a `RomSet`, as `KS/tests/ks32_firmware.h`)
- Modify: `CMakeLists.txt` (`add_library(ens_ts STATIC …)` linking `ens_otto ens_esp ens_m68k ens_duart`), `tests/CMakeLists.txt`, `docs/decisions.md` (T2, T3), `docs/design/2026-11-13-ts-emulator-design.md` section 9 "OS ROM" bullet and `roms/README.txt` (replace the mirror text with T2)

**Interfaces** (`namespace ens::ts`):

```cpp
struct RomSet {                         // file images, unmodified
    std::vector<uint8_t> osHigh, osLow; // 256 KB; osLow 256 KB or 512 KB
    std::vector<uint8_t> u41, u43, u45, u46;  // 2 MB, 1 MB, 2 MB, 1 MB
};
bool buildOsImage(const RomSet&, std::vector<uint16_t>& words, std::string& err);  // 256K words (T2)
class WaveMemory : public OttoMemory {  // T3, T4
public:
    static constexpr uint32_t kSampleRamWords = 0x100000;
    bool load(const RomSet&, std::string& err);  // err names the file: "u45: expected 2097152 bytes, got …"
    uint16_t read(int bank, uint32_t addr21) override;
    uint16_t& sampleRam(uint32_t word);          // the CPU's host window (Task 4 maps it)
};
```

- [ ] **Step 1: Failing tests** (`tests/test_ts_roms.cpp`, synthetic images unless noted):
  - `ts_os_image_interleaves_lower_half`: h = {0x11, 0x22,…}, l (512 KB) with lower half {0xA1, 0xA2,…} and upper half 0xFF → `words[0]==0x11A1`, `words[1]==0x22A2`, `words.size()==0x40000`; a 256 KB l gives the same words.
  - `ts_bank0_builds_12_bit_samples`: u41[5]=0x12, u43[5]=0x34 → bank 0 addr 5 = 0x1240; addr 0x100005 = `u41[0x100005]<<8 | 0x30`.
  - `ts_bank1_regions`: U45 word region below 0x100000, U46 region 0x100000-0x17FFFF, 0 above.
  - `ts_sample_ram_bank2`: `sampleRam(7) = 0xBEEF` → `read(2, 7)==0xBEEF`; `read(2, 0x100000)==0`; `read(3, 0)==0`.
  - Review focus `ts_roms_wrong_size_named`: each of the six files one byte short → `load`/`buildOsImage` false and `err` starts with that file's name (`osHigh`, `osLow`, `u41`, `u43`, `u45`, `u46`).
  - `ts_word_rom_byte_order` (real ROMs from `ENS_ROM_DIR`): for U45 and U46, the mean `|s[i+1]-s[i]|` over all words read as signed 16-bit is computed for both byte orders; the order the loader uses must be the smaller one, by at least a factor of 2. Print both means.
- [ ] **Step 2:** Build and run `build/ens_tests.exe ts_`. Expected: compile FAIL.
- [ ] **Step 3:** Implement per T2, T3, T4. Settle the U45/U46 byte order by the test, record it in T3's entry with the two printed means.
- [ ] **Step 4:** Run the whole suite. Expected: `244 passed, 0 failed` (238 + 6).
- [ ] **Step 5:** Commit `feat(ts): ROM set, OS image, wave memory` with T2, T3 and the design/README fix.

### Task 2: Board: OTTO, ESP, delay DRAM and audio routing

**Files:**
- Create: `src/ts/board.h`, `src/ts/board.cpp`, `tests/test_ts_board.cpp`
- Modify: `docs/decisions.md` (T11)

**Interfaces:**

```cpp
constexpr int kEspSlotsPerFrame = 103;   // T11
struct Audio { int16_t mainL, mainR, auxL, auxR; };
class Board : public EspBus {             // owns the sound chips and the ESP delay DRAM (64K x 16)
public:
    explicit Board(WaveMemory& waves);
    Otto& otto(); Esp& esp();
    Audio route(const OttoFrame& f, int espSlots);  // T11; espSlots = 0 while the ESP is halted
};
```

(`EspBus` is the ESP's memory interface in `src/esp/esp.h`; implement it as the KS-32 `Board` does.)

- [ ] **Step 1: Failing tests:** `ts_route_otto_to_esp_inputs` (a pass-through ESP program from `tests/test_esp_programs.cpp`'s style copying SER0→SER1 makes OTTO ch0 L = 0x12340 (20-bit) appear on `mainL` as 0x1234); `ts_route_aux_is_otto_ch3` (ch3 = (-0x12340, 0x7FFFF) → aux = (-0x1234, 0x7FFF)); `ts_route_saturates` (ch3 L = −0x80000, the 20-bit minimum → aux −32768; ch3 R = 0x7FFFF → 32767); `ts_route_drops_ch4_ch5`; `ts_esp_dram_is_64k_words` (an ESP RAM write at word 0xFFFF and at 0x10000 (wraps to 0) through the EspBus).
- [ ] **Step 2:** Run. Expected: compile FAIL.
- [ ] **Step 3:** Implement per T11.
- [ ] **Step 4:** Run the suite. Expected: `249 passed, 0 failed`.
- [ ] **Step 5:** Commit `feat(ts): board, ESP delay DRAM, audio routing` with T11.

### Task 3: Machine skeleton, bus trace and `tsemu boot`

**Files:**
- Create: `src/ts/machine.h`, `src/ts/machine.cpp`, `tests/test_ts_machine.cpp`, `tools/tsemu/main.cpp` (adapted from `KS/tools/ks32emu/main.cpp`: `boot` only in this task), `tools/common/files.{h,cpp}` (from `KS/tools/ks32play/files.*`: `readFile` only)
- Modify: `CMakeLists.txt` (`tsemu`), `docs/decisions.md` (T1, T7, T14)

**Interfaces** (mirroring `KS/src/ks32/machine.h`; names fixed here for later tasks):

```cpp
enum class Device : uint8_t { Otto, Esp, Duart, Fdc, Iexp, Scsi, Unmapped, Iack };
struct BusEvent { uint64_t cycle; uint32_t addr; uint16_t value; uint8_t size; bool write; Device dev; };
constexpr uint32_t kCpuHz = 15'238'090, kDuartHz = 4'000'000;   // T1
constexpr int kSlotClocks = 16, kPanelChannel = 1, kMidiChannel = 0;
class Machine : public m68k::Bus {
public:
    static std::unique_ptr<Machine> create(const RomSet&, std::string& err);
    void runFrame(Audio& out);                 // one OTTO frame (ACTV+1 slots)
    uint64_t cycles() const;
    uint16_t peek16(uint32_t addr) const;      // ROM/RAM only, no side effects
    std::function<void(const BusEvent&)> onBusEvent;
    m68k::Cpu& cpu(); Board& board(); Duart& duart();
    // Tasks 5-8 add: panel(), button(), key(), setAnalog(), midiIn(), popMidiOut(), ramImage(), loadRamImage().
};
```

- The scheduler is the KS-32 `slot()` (`KS/src/ks32/machine.cpp`): per voice slot the CPU runs `16 - carry` clocks (carrying the overshoot; a carry ≥ 16 skips the CPU for that slot), then `otto.stepVoice()`, then the DUART advances by the exact rational accumulator (`acc += 16 * kDuartHz; x1 = acc / kCpuHz`), then `updateLines()` (IPL from the asserting devices, ESP HALT from OP6, OTTO pot from the mux). After the frame: `board.route(otto.takeFrame(), esp halted ? 0 : kEspSlotsPerFrame)`.
- The initial map holds only what the reset vector proves: the 512 KB OS image at 0xC00000 (mirrored over 0xC00000-0xCFFFFF) and readable at 0 for the reset fetch; 64 KB of RAM at 0xFF0000 (T13 is settled in Task 4). Every other access is `Device::Unmapped`, reads 0, and is traced.

- [ ] **Step 1: Failing tests** with tiny hand-assembled ROMs (KS-32 `romWith` in `KS/tests/test_ks32_machine.cpp`): `ts_machine_rejects_bad_roms` (null + named file); `ts_ram_write_reads_back` (a ROM that writes 0x1234 to 0xFF1000 and loops → `peek16(0xFF1000)==0x1234`); `ts_unmapped_access_is_traced` (one write to 0x500000 → exactly one `Unmapped` event with that address, value and size); Review focus `ts_frame_cadence` (100 frames with ACTV 31 → `cycles()` in 51 140..51 260, for a ROM looping on `NOP` and one looping on `DIVU` (worst-case instruction time)); `ts_peek_has_no_side_effects`.
- [ ] **Step 2:** Run. Expected: compile FAIL.
- [ ] **Step 3:** Implement the machine and `tsemu boot --roms <dir> --ms N --trace unmapped,iack,duart,otto,esp --last N` with the KS-32 summary (exception counts per vector, distinct unmapped address runs, DUART register writes, OTTO active voices, ESP halted, bytes sent on channels A and B). Decisions T1, T7, T14.
- [ ] **Step 4:** Run the suite (`254 passed`) and `build/tsemu.exe boot --roms roms --ms 200`: it runs without crashing and prints a summary (unmapped runs are expected here).
- [ ] **Step 5:** Commit `feat(ts): machine skeleton, bus trace, tsemu boot`.

### Task 4: Firmware bring-up: address map, RAM, interrupts, devices

The method is `KS/docs/ks32-hardware-interface.md` §1-5: run `tsemu boot --ms 5000 --trace …`, read the summary, and change one thing at a time. **"Gets further"** = new kinds of device access, fewer unmapped accesses or fault vectors, and none of the firmware's error texts or codes on channel B (17 unexpected OTTO interrupt, 128 bus error, 137/138 spurious interrupt, 145 unknown DUART interrupt, 32-35 ESP download/chip/DRAM). Candidates are limited by the schematic: I/O1 devices sit at A19:A18 = 0..3 of one region, OTTO/ESP at A19 = 0/1 of another, all on odd bytes; PAL U68 decodes on A15-A23 (32 KB granularity).

**Files:**
- Create: `tests/test_ts_firmware.cpp`, `docs/ts-hardware-interface.md`
- Modify: `src/ts/machine.{h,cpp}`, `src/ts/roms.cpp` (host window), `tests/ts_firmware.h` (`bootedTs()`, `traceHash(m, ms)` (FNV-1a 64 over every BusEvent field as 8 little-endian bytes, as KS-32), `runMs`), `docs/decisions.md` (T4, T5, T6, T12, T13)

- [ ] **Step 1: Write the boot tests now** (they fail on the skeleton), each running `kBootMs` (start at 4000; set to the measured boot time + 50 % in Step 4):
  - `ts_boot_is_fully_mapped`: 0 `Unmapped` events; no byte access to an even OTTO/ESP/DUART address.
  - `ts_boot_takes_no_fault_vectors`: exception counts for vectors 2-8, 11, 14, 15, 24 are 0; the CPU is not halted.
  - `ts_boot_runs_the_chips`: ESP not halted at the end; `activeVoices()` equals the value the firmware sets (record it); the DUART's channel A and B are enabled.
  - `ts_boot_is_deterministic`: two fresh machines give the same `traceHash`; print `boot trace hash %016llx`.
  - Review focus `ts_two_machines_are_independent`: boot two machines interleaved frame by frame → both hashes equal the single-machine hash.
- [ ] **Step 2:** Run. Expected: FAIL (unmapped accesses).
- [ ] **Step 3: Iterate with the method** until all five pass: place each device (DUART first: the panel link and timers depend on it; then RAM sizes/mirrors/overlay (T13), OTTO, ESP, FDC, IEXP, the sample-RAM host window (T4), SCSI (T12)); then interrupt levels (T5); add wait states (T6) only if a timing check fails without them. Write each fact into `docs/ts-hardware-interface.md` with how it was found and what was ruled out, and each judgement into its T entry. If the CPU clock (T1) is wrong the firmware's clock checks will show it (e.g. a voice-count or timer error): record what it did.
- [ ] **Step 4:** Run the suite (`259 passed`) and record the boot hash.
- [ ] **Step 5:** Commit `feat(ts): firmware boots: address map, interrupts, devices` (one commit per settled device is fine).

### Task 5: Display and keyboard link

**Files:**
- Create: `src/ts/panel.h`, `src/ts/panel.cpp`, `tests/test_ts_panel.cpp`
- Modify: `src/ts/machine.{h,cpp}`, `tools/tsemu/main.cpp` (`--display-only`, `--press <ms>:A+B`), `docs/ts-hardware-interface.md`, `docs/decisions.md` (new T entries as found)

**Interfaces:**

```cpp
struct PanelState {                 // trivially copyable (snapshotted by the Plan 4 engine)
    char text[2][40]; uint64_t blink[2]; uint32_t leds; uint32_t unknown;  // bytes not decoded
};
enum class Button : uint8_t;        // every front-panel button, from the Musician's Manual names
class Panel {
public:
    void fromCpu(uint8_t b);        // DUART B TX -> display board
    bool popToCpu(uint8_t& b);      // keyboard microcontroller -> DUART B RX (fixed ring)
    void setLines(bool kres, bool dres, bool sack);  // OP4, OP5, OP7 as seen at the pins
    void button(Button, bool down); void key(int note, bool down, int velocity);
    void pressure(int note, int amount);             // Poly-Key
    const PanelState& state() const;
};
```

- [ ] **Step 1: Failing tests:** `ts_boot_shows_play_screen` (both rows equal the observed play-screen text, recorded in the test as observed with a comment saying so; `unknown == 0`, so every byte of the boot was decoded); `ts_panel_commands` (one test per decoded command, with the byte sequences that define it).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3: Find the display protocol** from the timestamped channel B TX bytes (KS-32 §"Panel protocol"): characters, cursor, clear, LEDs, blink. **Find the keyboard side:** what the firmware expects at power-up (does it wait for a byte after KRES/SACK?), then button codes (send each candidate code from several screens and diff the display, LEDs and MIDI out) and key codes (key down/up, velocity, pressure; check by MIDI out). Use the Musician's Manual button names for `Button`. Record SACK/'s role. Add tests as each part is settled: `ts_panel_button_bytes`, `ts_button_changes_page` (e.g. pressing a mode button shows its page), `ts_key_sounds` (a key-down produces a note on MIDI out and audio above the noise floor), `ts_poly_pressure_is_sent`.
- [ ] **Step 4:** Run the suite; record the count.
- [ ] **Step 5:** Commit per settled part, e.g. `feat(ts): display protocol`, `feat(ts): keyboard and buttons`.

### Task 6: Analog inputs, IEXP port and the empty floppy drive

**Files:**
- Modify: `src/ts/machine.{h,cpp}`, `tools/tsemu/main.cpp` (`--analog NAME=raw`), `tests/test_ts_firmware.cpp`, `docs/ts-hardware-interface.md`, `docs/decisions.md` (T8, T9, T10)

**Interfaces:** `enum class Analog : uint8_t { PitchWheel, ModWheel, Volume, CvPedal, DataEntry, Battery, Reference };` with the mux inputs of **Hardware facts**; `void setAnalog(Analog, uint16_t raw10)`; `void footswitch(int n, bool down)` (FSW3/FSW4 on IEXP; FSW1/FSW2 go to the keyboard and are Task 5's); `void patchSelect(int side, bool down)`.

- [ ] **Step 1: Failing tests:** `ts_cold_boot_is_clean` (no low-battery or "Event"/error text on any frame of the boot); `ts_analog_resting_values` (the Analog Test page, reached as the service manual describes, shows REF in 180-220 and the resting readings chosen in T8); `ts_slider_moves_volume` (raw 0 vs 0x3FF on Volume changes the audio level of a held note); `ts_patch_select_is_read` (pressing a patch select button changes what a played note sounds like or shows, as observed); `ts_floppy_reports_no_disk` (a disk operation from the panel shows the firmware's no-disk response and no error code).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Find the mux timing and resting values (KS-32 K8/K13 method: one select at a time at 0 or 0x3FF, read the Analog Test page); settle IEXP's upper bits; give the FDC and DSKCHG/DENS/DSTAT what the firmware needs for an empty drive, recorded register by register.
- [ ] **Step 4:** Run the suite; record the count.
- [ ] **Step 5:** Commit `feat(ts): analog inputs, footswitches, patch select, empty floppy`.

### Task 7: MIDI, `tsemu render` and the Plan 2 rechecks

**Files:**
- Create: `tests/ts_render.h` (shared `pcmHash`, `sendMidi`, render loop for tests and the tool: one copy, not two)
- Modify: `src/ts/machine.{h,cpp}` (`midiIn`, `popMidiOut`), `tools/tsemu/main.cpp` (`render in.mid out.wav --tail ms`, `bench`), `tests/test_ts_firmware.cpp`, `docs/verification-plan2.md` ("Left for Plan 3" answered), `docs/decisions.md` (update OT entries' Status/Confidence with what the firmware showed)

- `render`: boot `kBootMs` (not written), feed SMF events at their frames, render to the last event + tail, write a 16-bit stereo WAV of Main (and `--aux out2.wav`) at 29 762 Hz, print `pcm hash %016llx`. Reuse `KS/src/player/smf.*` and `wav.*` by copying them into `tools/common/` (file I/O stays out of `src/`).

- [ ] **Step 1: Failing tests:** `ts_midi_rate` (channel A runs at 31 250 baud); `ts_midi_note_sounds` (peak below 64 before a note-on, above 1000 after); Review focus `ts_midi_burst_no_overrun` (200 note-on/off bytes as fast as the wire allows → 0 overruns, no "out of buffers" text); `ts_program_change_changes_display`; `ts_render_is_deterministic` (two renders of `tests/data/player_smoke.mid`, copied from KS-32, give the same PCM; print the hash).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement; then answer each Plan 2 "Left for Plan 3" item from the running firmware and write the answers into `docs/verification-plan2.md` and the OT entries:
  - OT7: count OTTO filter-temp reads of stopped voices during boot and play.
  - OT1: the byte order and sizes of the firmware's OTTO accesses (from the trace).
  - OT16: when the firmware reads PAR relative to its mux changes.
  - OT9/OT10/OT13/OT12: render a filtered patch and a full chord; listen; record the result as "user to run" where a person is needed.
- [ ] **Step 4:** Run the suite and `tsemu render tests/data/player_smoke.mid out.wav --roms roms`; record both hashes.
- [ ] **Step 5:** Commit `feat(ts): MIDI, render, Plan 2 rechecks`.

### Task 8: Battery RAM, verification and merge

**Files:**
- Modify: `src/ts/machine.{h,cpp}` (`ramImage()`, `loadRamImage()` per T13: a power cycle keeping RAM), `tools/tsemu/main.cpp` (`--ram file`), `tests/test_ts_firmware.cpp`, `AGENTS.md` (Testing: the reference hashes and `tsemu`), `README.md` (Status)
- Create: `docs/verification-plan3.md`

- [ ] **Step 1: Failing tests:** `ts_ram_image_keeps_settings` (change Master Tune on its page, power-cycle with `loadRamImage(ramImage())`, the page shows the changed value); Review focus `ts_garbage_ram_boots` (a RAM image of pseudo-random bytes (fixed seed) boots to the play screen with no fault vector).
- [ ] **Step 2:** Run. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4: Both toolchains** from empty build folders: all tests pass on MinGW i686 and MSVC x64 with 0 warnings and the same names; the boot trace hash, the `ts_render_is_deterministic` hash and the `tsemu render` pcm hash are identical on both. `tsemu bench`: at least ×2 real time on MSVC (record it).
- [ ] **Step 5:** Write `docs/verification-plan3.md` (KS-32 `verification-plan6.md` shape): toolchains, counts, the three hashes, every T decision with confidence, the hardware facts found and how, the Plan 2 answers, what is left for Plan 4 (the engine and the app), and "user to run" items (listening checks).
- [ ] **Step 6:** Commit `docs: plan 3 verification`; merge: `git checkout main && git merge --no-ff plan3-ts-machine -m "Merge branch 'plan3-ts-machine': TS-10 machine boots and plays"`.
